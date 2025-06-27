#ifndef FITVIEWEXTENSION_H
#define FITVIEWEXTENSION_H

#include <avogadro/extension.h>
#include <QAction>
#include <QList>

namespace Avogadro {

class FitViewExtension : public Extension
{
  Q_OBJECT
  AVOGADRO_EXTENSION("FitView", tr("Fit View"),
                     tr("Center and zoom to fit the molecule"))

public:
  explicit FitViewExtension(QObject *parent = 0);
  ~FitViewExtension();

  QList<QAction *> actions() const;
  QUndoCommand *performAction(QAction *action, GLWidget *widget);
  QString menuPath(QAction *action) const;

private:
  QList<QAction *> m_actions;
};

class FitViewExtensionFactory : public QObject, public PluginFactory
{
  Q_OBJECT
  Q_INTERFACES(Avogadro::PluginFactory)
  Q_PLUGIN_METADATA(IID "net.sourceforge.avogadro.pluginfactory/1.5")
  AVOGADRO_EXTENSION_FACTORY(FitViewExtension)
};

} // namespace Avogadro

#endif // FITVIEWEXTENSION_H
