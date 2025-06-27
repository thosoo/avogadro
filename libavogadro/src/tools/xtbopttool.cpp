#include "xtbopttool.h"

#include <avogadro/glwidget.h>
#include <avogadro/camera.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>
#include <avogadro/painter.h>

#include <QtWidgets/QAction>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QProgressBar>
#include <QtCore/QElapsedTimer>
#include <Eigen/Core>
#include <QtCore/QMutexLocker>

#include <xtb.h>
#ifdef _OPENMP
#  include <omp.h>
#endif

namespace Avogadro {

XtbOptTool::XtbOptTool(QObject *parent)
  : Tool(parent), m_glwidget(nullptr), m_thread(new XtbOptThread),
    m_settingsWidget(nullptr), m_running(false),
    m_setupFailed(false), m_threadsSpinBox(nullptr),
    m_comboEngine(nullptr), m_comboLevel(nullptr), m_comboMethod(nullptr),
    m_progressBar(nullptr), m_lastEnergy(0.0), m_deltaEnergy(0.0)
{
  QAction *action = activateAction();
  action->setIcon(QIcon(QString::fromUtf8(":/xtbopttool/autoopttool.png")));
  action->setToolTip(tr("xTB Optimization Tool"));

  connect(m_thread, SIGNAL(finished(bool)), this, SLOT(finished(bool)));
  connect(m_thread, SIGNAL(setupDone()), this, SLOT(setupDone()));
  connect(m_thread, SIGNAL(setupFailed()), this, SLOT(setupFailed()));
  connect(m_thread, SIGNAL(setupSucces()), this, SLOT(setupSucces()));
  connect(m_thread, SIGNAL(finished()), this, SLOT(threadFinished()));
  connect(m_thread, SIGNAL(progress(int,int,double)), this, SLOT(updateProgress(int,int,double)));
}

XtbOptTool::~XtbOptTool()
{
  if (m_thread) {
    m_thread->stop();
    m_thread->wait();
    m_thread->cleanup();
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

QUndoCommand* XtbOptTool::mouseReleaseEvent(GLWidget *widget, QMouseEvent *)
{
  m_glwidget = widget;
  m_clickedAtom = nullptr;
  return nullptr;
}

QUndoCommand* XtbOptTool::mouseMoveEvent(GLWidget *widget, QMouseEvent *event)
{
  m_glwidget = widget;
  if (m_running && m_clickedAtom) {
    QPoint from = m_lastDraggingPosition;
    QPoint to = event->pos();
    translate(widget, *m_clickedAtom->pos(), from, to);
    m_lastDraggingPosition = to;
  }
  return nullptr;
}

QUndoCommand* XtbOptTool::mouseDoubleClickEvent(GLWidget *widget, QMouseEvent *)
{
  m_glwidget = widget;
  return nullptr;
}

QUndoCommand* XtbOptTool::wheelEvent(GLWidget *widget, QWheelEvent *)
{
  m_glwidget = widget;
  return nullptr;
}

void XtbOptTool::translate(GLWidget *widget, const Eigen::Vector3d &what,
                           const QPoint &from, const QPoint &to) const
{
  Eigen::Vector3d fromPos = widget->camera()->unProject(from, what);
  Eigen::Vector3d toPos = widget->camera()->unProject(to, what);
  Eigen::Vector3d atomTranslation = toPos - fromPos;
  if (m_clickedAtom) {
    QMutexLocker locker(&m_thread->mutex());
    m_clickedAtom->setPos(atomTranslation + *m_clickedAtom->pos());
    m_clickedAtom->update();
  }
}

bool XtbOptTool::paint(GLWidget *widget)
{
  m_glwidget = widget;
  if (m_running || m_setupFailed) {
    glColor3f(1.0, 1.0, 1.0);
    if (m_setupFailed) {
      widget->painter()->drawText(QPoint(10, 20), tr("xTB setup failed"));
    } else {
      const double factor = 2625.499748;
      double e = m_lastEnergy * factor;
      double de = m_deltaEnergy * factor;
      widget->painter()->drawText(QPoint(10, 20),
                                  tr("xTB: E = %1 kJ/mol (dE = %2)")
                                      .arg(e)
                                      .arg(de));
    }
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

    QLabel *engineLabel = new QLabel(tr("Engine:"));
    m_comboEngine = new QComboBox(m_settingsWidget);
    m_comboEngine->addItem(tr("Gradient Descent"));
    m_comboEngine->addItem(tr("L-BFGS"));
    m_comboEngine->addItem(tr("ANCopt"));
    m_comboEngine->setCurrentIndex(2);

    QLabel *levelLabel = new QLabel(tr("Level:"));
    m_comboLevel = new QComboBox(m_settingsWidget);
    m_comboLevel->addItem(tr("Crude"));
    m_comboLevel->addItem(tr("Sloppy"));
    m_comboLevel->addItem(tr("Loose"));
    m_comboLevel->addItem(tr("Lax"));
    m_comboLevel->addItem(tr("Normal"));
    m_comboLevel->addItem(tr("Tight"));
    m_comboLevel->addItem(tr("Vtight"));
    m_comboLevel->addItem(tr("Extreme"));
    m_comboLevel->setCurrentIndex(4);

    QLabel *methodLabel = new QLabel(tr("Method:"));
    m_comboMethod = new QComboBox(m_settingsWidget);
    m_comboMethod->addItem(tr("GFN0-xTB"));
    m_comboMethod->addItem(tr("GFN1-xTB"));
    m_comboMethod->addItem(tr("GFN2-xTB"));
    m_comboMethod->addItem(tr("GFN-FF"));
    m_comboMethod->setCurrentIndex(2);

    QLabel *threadsLabel = new QLabel(tr("Threads:"));
    m_threadsSpinBox = new QSpinBox(m_settingsWidget);
    m_threadsSpinBox->setMinimum(1);
    m_threadsSpinBox->setMaximum(64);
    m_threadsSpinBox->setValue(1);

    m_buttonStartStop = new QPushButton(tr("Start"), m_settingsWidget);
    m_progressBar = new QProgressBar(m_settingsWidget);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(label);
    layout->addWidget(m_stepsSpinBox);
    layout->addWidget(threadsLabel);
    layout->addWidget(m_threadsSpinBox);
    layout->addWidget(engineLabel);
    layout->addWidget(m_comboEngine);
    layout->addWidget(levelLabel);
    layout->addWidget(m_comboLevel);
    layout->addWidget(methodLabel);
    layout->addWidget(m_comboMethod);
    layout->addWidget(m_buttonStartStop);
    layout->addWidget(m_progressBar);
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

  if (!m_glwidget || !m_glwidget->molecule()) {
    emit message(tr("No active molecule for xTB optimization"));
    return;
  }

  connect(m_glwidget->molecule(), SIGNAL(destroyed()), this, SLOT(abort()));

  QByteArray nt = QByteArray::number(m_threadsSpinBox ? m_threadsSpinBox->value() : 1);
  qputenv("OMP_NUM_THREADS", nt);
#ifdef _OPENMP
  omp_set_num_threads(m_threadsSpinBox ? m_threadsSpinBox->value() : 1);
#endif

  int method = m_comboMethod ? m_comboMethod->currentIndex() : 2;
  int engine = m_comboEngine ? m_comboEngine->currentIndex() : 2;
  int level = m_comboLevel ? m_comboLevel->currentIndex() : 4;
  if (!m_thread->setup(m_glwidget->molecule(), method, engine, level,
                        m_stepsSpinBox->value())) {
    m_setupFailed = true;
    emit message(tr("Failed to initialize xTB"));
    return;
  }
  m_setupFailed = false;
  m_thread->start();
  m_running = true;
  m_buttonStartStop->setText(tr("Stop"));
  if (m_progressBar) {
    m_progressBar->setRange(0, m_stepsSpinBox->value());
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
  }
  m_lastEnergy = 0.0;
  m_deltaEnergy = 0.0;
}

void XtbOptTool::abort()
{
  disable();
}

void XtbOptTool::disable()
{
  if (!m_running)
    return;

  m_thread->stop();
  if (m_buttonStartStop)
    m_buttonStartStop->setEnabled(false);
  if (m_progressBar)
    m_progressBar->setVisible(false);
  m_lastEnergy = 0.0;
  m_deltaEnergy = 0.0;
}

void XtbOptTool::finished(bool)
{
  if (m_glwidget && m_glwidget->molecule()) {
    QMutexLocker locker(&m_thread->mutex());
    const std::vector<double> &coords = m_thread->coords();
    int natoms = m_thread->natoms();
    QList<Atom *> atoms = m_glwidget->molecule()->atoms();
    if (atoms.size() >= natoms) {
      const double bohr2ang = 0.52917721092;
      for (int i = 0; i < natoms; ++i) {
        double x = coords[3 * i] * bohr2ang;
        double y = coords[3 * i + 1] * bohr2ang;
        double z = coords[3 * i + 2] * bohr2ang;
        atoms[i]->setPos(Eigen::Vector3d(x, y, z));
      }
    }
    m_glwidget->molecule()->update();
    m_glwidget->update();
  }
}

void XtbOptTool::setupDone()
{
  // no timer needed when running in background thread
}

void XtbOptTool::setupFailed()
{
  m_setupFailed = true;
  emit message(tr("xTB initialization failed"));
  disable();
  if (m_glwidget)
    m_glwidget->update();
}

void XtbOptTool::setupSucces()
{
  m_setupFailed = false;
}

void XtbOptTool::threadFinished()
{
  m_thread->cleanup();
  m_running = false;
  m_setupFailed = false;
  if (m_buttonStartStop)
    m_buttonStartStop->setText(tr("Start"));
  if (m_buttonStartStop)
    m_buttonStartStop->setEnabled(true);
  if (m_progressBar)
    m_progressBar->setVisible(false);
  m_lastEnergy = 0.0;
  m_deltaEnergy = 0.0;
  if (m_glwidget)
    m_glwidget->update();
}

void XtbOptTool::updateProgress(int step, int total, double energy)
{
  if (m_progressBar) {
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(step);
  }
  m_deltaEnergy = energy - m_lastEnergy;
  m_lastEnergy = energy;
  if (m_glwidget)
    m_glwidget->update();
}

XtbOptThread::XtbOptThread(QObject *parent)
  : QThread(parent), m_molecule(nullptr), m_env(nullptr), m_xtbMol(nullptr),
    m_calc(nullptr), m_results(nullptr), m_stop(false)
{
}

XtbOptThread::~XtbOptThread()
{
  if (m_results)
    xtb_delResults(&m_results);
  if (m_calc)
    xtb_delCalculator(&m_calc);
  if (m_xtbMol)
    xtb_delMolecule(&m_xtbMol);
  if (m_env)
    xtb_delEnvironment(&m_env);
}

void XtbOptThread::cleanup()
{
  if (m_results) {
    xtb_delResults(&m_results);
    m_results = nullptr;
  }
  if (m_calc) {
    xtb_delCalculator(&m_calc);
    m_calc = nullptr;
  }
  if (m_xtbMol) {
    xtb_delMolecule(&m_xtbMol);
    m_xtbMol = nullptr;
  }
  if (m_env) {
    xtb_delEnvironment(&m_env);
    m_env = nullptr;
  }
}

bool XtbOptThread::setup(Molecule *mol, int method, int engine, int level,
                         int steps)
{
  m_method = method;
  m_engine = engine;
  m_level = level;
  m_mutex.lock();
  m_molecule = mol;
  m_steps = steps;
  m_stop = false;

  m_env = xtb_newEnvironment();
  if (!m_env) {
    m_mutex.unlock();
    emit setupFailed();
    return false;
  }
  int natoms = m_molecule ? m_molecule->numAtoms() : 0;
  m_numbers.resize(natoms);
  m_coords.resize(natoms * 3);
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
  if (!m_xtbMol || !m_calc) {
    m_mutex.unlock();
    emit setupFailed();
    return false;
  }
  switch (m_method) {
  case 0:
    xtb_loadGFN0xTB(m_env, m_xtbMol, m_calc, NULL);
    break;
  case 1:
    xtb_loadGFN1xTB(m_env, m_xtbMol, m_calc, NULL);
    break;
  case 2:
    xtb_loadGFN2xTB(m_env, m_xtbMol, m_calc, NULL);
    break;
  default:
    xtb_loadGFNFF(m_env, m_xtbMol, m_calc, NULL);
    break;
  }
  m_results = xtb_newResults();
  if (!m_results) {
    m_mutex.unlock();
    emit setupFailed();
    return false;
  }
  m_mutex.unlock();
  emit setupSucces();
  emit setupDone();
  return true;
}

void XtbOptThread::run()
{
  while (!m_stop) {
    update();
  }
}

void XtbOptThread::update()
{
  int natoms;
  {
    QMutexLocker locker(&m_mutex);
    if (!m_env || !m_xtbMol || !m_calc || !m_results || !m_molecule)
      return;

    natoms = m_numbers.size();
    const double ang2bohr = 1.8897259886;
    for (int i = 0; i < natoms; ++i) {
      const Atom *a = m_molecule->atom(i);
      Eigen::Vector3d p = *a->pos();
      m_coords[3 * i] = p.x() * ang2bohr;
      m_coords[3 * i + 1] = p.y() * ang2bohr;
      m_coords[3 * i + 2] = p.z() * ang2bohr;
    }
  }

  std::vector<double> localCoords;
  {
    QMutexLocker locker(&m_mutex);
    localCoords = m_coords;
  }

  QElapsedTimer timer;
  timer.start();
  for (int s = 0; s < m_steps && !m_stop; ++s) {
    double energy = 0.0;

    xtb_updateMolecule(m_env, m_xtbMol, localCoords.data(), NULL);
    xtb_singlepoint(m_env, m_xtbMol, m_calc, m_results);
    xtb_getEnergy(m_env, m_results, &energy);

    std::vector<double> grad(natoms * 3);
    xtb_getGradient(m_env, m_results, grad.data());

    double gradNorm = 0.0;
    for (int i = 0; i < natoms * 3; ++i)
      gradNorm += grad[i] * grad[i];
    gradNorm = std::sqrt(gradNorm);

    double step = 0.1;
    if (gradNorm > 1.0)
      step /= gradNorm;
    for (int i = 0; i < natoms * 3; ++i)
      localCoords[i] -= step * grad[i];

    {
      QMutexLocker locker(&m_mutex);
      m_coords = localCoords;
    }

    emit progress(s + 1, m_steps, energy);
    if (timer.elapsed() >= 1000) {
      emit finished(true);
      timer.restart();
    }
  }
  emit finished(true);
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
  if (m_threadsSpinBox)
    settings.setValue("threads", m_threadsSpinBox->value());
  if (m_comboEngine)
    settings.setValue("engine", m_comboEngine->currentIndex());
  if (m_comboLevel)
    settings.setValue("level", m_comboLevel->currentIndex());
  if (m_comboMethod)
    settings.setValue("method", m_comboMethod->currentIndex());
}

void XtbOptTool::readSettings(QSettings &settings)
{
  Tool::readSettings(settings);
  if (m_stepsSpinBox)
    m_stepsSpinBox->setValue(settings.value("steps", 4).toInt());
  if (m_threadsSpinBox)
    m_threadsSpinBox->setValue(settings.value("threads", 1).toInt());
  if (m_comboEngine)
    m_comboEngine->setCurrentIndex(settings.value("engine", 2).toInt());
  if (m_comboLevel)
    m_comboLevel->setCurrentIndex(settings.value("level", 4).toInt());
  if (m_comboMethod)
    m_comboMethod->setCurrentIndex(settings.value("method", 2).toInt());
}

} // namespace Avogadro
