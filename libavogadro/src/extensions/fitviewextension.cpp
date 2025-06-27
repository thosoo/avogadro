#include "fitviewextension.h"
#include <avogadro/glwidget.h>
#include <avogadro/camera.h>

#include <QAction>

namespace Avogadro {

FitViewExtension::FitViewExtension(QObject *parent)
  : Extension(parent)
{
  QAction *action = new QAction(this);
  action->setText(tr("Fit to Screen"));
  m_actions.append(action);
}

FitViewExtension::~FitViewExtension()
{
}

QList<QAction *> FitViewExtension::actions() const
{
  return m_actions;
}

QString FitViewExtension::menuPath(QAction *) const
{
  return tr("&View");
}

QUndoCommand *FitViewExtension::performAction(QAction *, GLWidget *widget)
{
  if (!widget)
    return 0;

  widget->camera()->initializeViewPoint();
  widget->update();
  return 0;
}

} // namespace Avogadro
