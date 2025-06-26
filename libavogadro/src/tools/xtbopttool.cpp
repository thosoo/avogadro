#include "xtbopttool.h"

#include <avogadro/glwidget.h>
#include <avogadro/camera.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>

#include <QtWidgets/QAction>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <Eigen/Core>

#include <xtb.h>

namespace Avogadro {

XtbOptTool::XtbOptTool(QObject *parent)
  : Tool(parent), m_glwidget(nullptr), m_thread(new XtbOptThread),
    m_settingsWidget(nullptr), m_timerId(0), m_running(false)
{
  QAction *action = activateAction();
  action->setIcon(QIcon(QString::fromUtf8(":/xtbopttool/autoopttool.png")));
  action->setToolTip(tr("xTB Optimization Tool"));

  connect(m_thread, SIGNAL(finished(bool)), this, SLOT(finished(bool)));
  connect(m_thread, SIGNAL(setupDone()), this, SLOT(setupDone()));
}

XtbOptTool::~XtbOptTool()
{
  if (m_thread) {
    m_thread->stop();
    m_thread->wait();
    delete m_thread;
  }
  if (m_settingsWidget)
    m_settingsWidget->deleteLater();
}

int XtbOptTool::usefulness() const
{
  return 10;
}

QUndoCommand* XtbOptTool::mousePressEvent(GLWidget *widget, QMouseEvent *event)
{
  m_glwidget = widget;
  m_lastDraggingPosition = event->pos();
  m_clickedAtom = widget->computeClickedAtom(event->pos());
  return nullptr;
}

QUndoCommand* XtbOptTool::mouseReleaseEvent(GLWidget *, QMouseEvent *)
{
  m_clickedAtom = nullptr;
  return nullptr;
}

QUndoCommand* XtbOptTool::mouseMoveEvent(GLWidget *widget, QMouseEvent *event)
{
  if (m_running && m_clickedAtom) {
    QPoint from = m_lastDraggingPosition;
    QPoint to = event->pos();
    translate(widget, *m_clickedAtom->pos(), from, to);
    m_lastDraggingPosition = to;
  }
  return nullptr;
}

QUndoCommand* XtbOptTool::mouseDoubleClickEvent(GLWidget *, QMouseEvent *)
{
  return nullptr;
}

QUndoCommand* XtbOptTool::wheelEvent(GLWidget *, QWheelEvent *)
{
  return nullptr;
}

void XtbOptTool::translate(GLWidget *widget, const Eigen::Vector3d &what,
                           const QPoint &from, const QPoint &to) const
{
  Eigen::Vector3d fromPos = widget->camera()->unProject(from, what);
  Eigen::Vector3d toPos = widget->camera()->unProject(to, what);
  Eigen::Vector3d atomTranslation = toPos - fromPos;
  if (m_clickedAtom) {
    m_clickedAtom->setPos(atomTranslation + *m_clickedAtom->pos());
    m_clickedAtom->update();
  }
}

bool XtbOptTool::paint(GLWidget *widget)
{
  if (m_running) {
    widget->painter()->setPen(Qt::white);
    widget->painter()->drawText(QPoint(10,20), tr("xTB optimizing..."));
  }
  return true;
}

QWidget* XtbOptTool::settingsWidget()
{
  if (!m_settingsWidget) {
    m_settingsWidget = new QWidget;

    QLabel *label = new QLabel(tr("Steps per Update:"));
    m_stepsSpinBox = new QSpinBox(m_settingsWidget);
    m_stepsSpinBox->setMinimum(1);
    m_stepsSpinBox->setMaximum(50);
    m_stepsSpinBox->setValue(4);

    m_buttonStartStop = new QPushButton(tr("Start"), m_settingsWidget);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(label);
    layout->addWidget(m_stepsSpinBox);
    layout->addWidget(m_buttonStartStop);
    layout->addStretch(1);
    m_settingsWidget->setLayout(layout);

    connect(m_buttonStartStop, SIGNAL(clicked()), this, SLOT(toggle()));
    connect(m_settingsWidget, SIGNAL(destroyed()), this, SLOT(settingsWidgetDestroyed()));
  }
  return m_settingsWidget;
}

void XtbOptTool::settingsWidgetDestroyed()
{
  m_settingsWidget = nullptr;
}

void XtbOptTool::toggle()
{
  if (m_running)
    disable();
  else
    enable();
}

void XtbOptTool::enable()
{
  if (m_running)
    return;

  m_thread->setup(m_glwidget->molecule(), 0, m_stepsSpinBox->value());
  m_thread->start();
  m_timerId = startTimer(50);
  m_running = true;
  m_buttonStartStop->setText(tr("Stop"));
}

void XtbOptTool::abort()
{
  disable();
}

void XtbOptTool::disable()
{
  if (!m_running)
    return;

  if (m_timerId)
    killTimer(m_timerId);
  m_timerId = 0;
  m_thread->stop();
  m_thread->wait();
  m_running = false;
  m_buttonStartStop->setText(tr("Start"));
}

void XtbOptTool::timerEvent(QTimerEvent *)
{
  if (m_running)
    m_thread->update();
}

void XtbOptTool::finished(bool)
{
  if (m_glwidget)
    m_glwidget->update();
}

void XtbOptTool::setupDone()
{
  if (!m_timerId)
    m_timerId = startTimer(50);
}

XtbOptThread::XtbOptThread(QObject *parent)
  : QThread(parent), m_molecule(nullptr), m_env(nullptr), m_xtbMol(nullptr),
    m_calc(nullptr), m_results(nullptr), m_stop(false)
{
}

XtbOptThread::~XtbOptThread()
{
  if (m_results)
    xtb_delResults(m_results);
  if (m_calc)
    xtb_delCalculator(m_calc);
  if (m_xtbMol)
    xtb_delMolecule(m_xtbMol);
  if (m_env)
    xtb_delEnvironment(m_env);
}

void XtbOptThread::setup(Molecule *mol, int algorithm, int steps)
{
  Q_UNUSED(algorithm);
  m_mutex.lock();
  m_molecule = mol;
  m_steps = steps;
  m_stop = false;

  m_env = xtb_newEnvironment();
  int natoms = m_molecule->numAtoms();
  m_numbers.resize(natoms);
  m_coords.resize(natoms*3);
  const double ang2bohr = 1.8897259886;
  for (int i=0;i<natoms;++i) {
    const Atom *a = m_molecule->atom(i);
    m_numbers[i] = a->atomicNumber();
    Eigen::Vector3d p = *a->pos();
    m_coords[3*i] = p.x()*ang2bohr;
    m_coords[3*i+1] = p.y()*ang2bohr;
    m_coords[3*i+2] = p.z()*ang2bohr;
  }
  double charge = 0.0;
  int uhf = 0;
  double lattice[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
  bool periodic[3] = {false,false,false};
  m_xtbMol = xtb_newMolecule(m_env, &natoms, m_numbers.data(), m_coords.data(),
                             &charge, &uhf, &lattice[0][0], periodic);
  m_calc = xtb_newCalculator();
  xtb_loadGFN2xTB(m_env, m_xtbMol, m_calc, NULL);
  m_results = xtb_newResults();
  m_mutex.unlock();
  emit setupDone();
}

void XtbOptThread::run()
{
  exec();
}

void XtbOptThread::update()
{
  QMutexLocker locker(&m_mutex);
  int natoms = m_numbers.size();
  const double bohr2ang = 0.52917721092;
  for (int s=0; s<m_steps && !m_stop; ++s) {
    xtb_updateMolecule(m_env, m_xtbMol, m_coords.data(), NULL);
    xtb_singlepoint(m_env, m_xtbMol, m_calc, m_results);
    std::vector<double> grad(natoms*3);
    xtb_getGradient(m_env, m_results, grad.data());
    double step = 0.1;
    for (int i=0;i<natoms*3;++i)
      m_coords[i] -= step*grad[i];
  }
  QList<Atom*> atoms = m_molecule->atoms();
  for (int i=0;i<natoms;++i) {
    double x = m_coords[3*i]*bohr2ang;
    double y = m_coords[3*i+1]*bohr2ang;
    double z = m_coords[3*i+2]*bohr2ang;
    atoms[i]->setPos(Eigen::Vector3d(x,y,z));
  }
}

void XtbOptThread::stop()
{
  m_stop = true;
}

XtbOptCommand::XtbOptCommand(Molecule *mol, XtbOptTool *tool, QUndoCommand *parent)
  : QUndoCommand(parent), m_moleculeCopy(*mol), m_molecule(mol), m_tool(tool)
{
  setText(QObject::tr("xTB Optimize"));
}

void XtbOptCommand::redo()
{
  m_moleculeCopy = *m_molecule;
}

void XtbOptCommand::undo()
{
  if (m_tool)
    m_tool->disable();
  *m_molecule = m_moleculeCopy;
}

bool XtbOptCommand::mergeWith(const QUndoCommand *)
{
  return true;
}

int XtbOptCommand::id() const
{
  return 1311388;
}

void XtbOptTool::writeSettings(QSettings &settings) const
{
  Tool::writeSettings(settings);
  if (m_stepsSpinBox)
    settings.setValue("steps", m_stepsSpinBox->value());
}

void XtbOptTool::readSettings(QSettings &settings)
{
  Tool::readSettings(settings);
  if (m_stepsSpinBox)
    m_stepsSpinBox->setValue(settings.value("steps", 4).toInt());
}

} // namespace Avogadro
