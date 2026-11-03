/**********************************************************************
  OrbitalExtension - Molecular orbital explorer

  Copyright (C) 2010 by David C. Lonie

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#ifndef ORBITALWIDGET_H
#define ORBITALWIDGET_H

#include <QWidget>

#include <algorithm>

#include <avogadro/primitive.h>
#include <avogadro/glwidget.h>

#include "orbitalextension.h"
#include "orbitaltablemodel.h"

#include "ui_orbitalwidget.h"

namespace Avogadro {
  class OrbitalSettingsDialog;
  class OrbitalTableModel;

  class OrbitalWidget : public QWidget
  {
      Q_OBJECT

    public:

      enum OrbitalQuality {
        OQ_Low = 0,
        OQ_Medium,
        OQ_High,
        OQ_VeryHigh
      };

      // Pin the ordering so clampPrecalcQuality's std::min(defaultQuality,
      // OQ_Medium) in OrbitalExtension::precalculateOrbitals (D11) selects
      // the intended coarser quality. Conjoined comparisons, not chained <:
      // (L < M < H) would promote (L < M) to int and vacuously pass.
      static_assert(static_cast<int>(OrbitalQuality::OQ_Low)
                        < static_cast<int>(OrbitalQuality::OQ_Medium)
                    && static_cast<int>(OrbitalQuality::OQ_Medium)
                        < static_cast<int>(OrbitalQuality::OQ_High)
                    && static_cast<int>(OrbitalQuality::OQ_High)
                        < static_cast<int>(OrbitalQuality::OQ_VeryHigh),
                    "OrbitalQuality must increase Low<Medium<High<VeryHigh");

      // D11: background precalculation runs at most at Medium resolution
      // regardless of the persisted default quality; an explicit per-orbital
      // request keeps the user-chosen quality ( OrbitalExtension passes its
      // queue resolution through this clamp). Unit-tested by precalcpolicytest.
      static OrbitalQuality clampPrecalcQuality(OrbitalQuality q)
      {
        return std::min(q, OQ_Medium);
      }

      //! Constructor
      explicit OrbitalWidget(QWidget *parent = nullptr,
                             Qt::WindowFlags f = Qt::WindowFlags());
      //! Deconstructor
      virtual ~OrbitalWidget();

      double isovalue() {return m_isovalue;};
      OrbitalQuality defaultQuality() {return m_quality;};
      // The quality currently selected in the widget's quality combo (D4): the
      // OrbitalExtension enqueues an explicit per-orbital request at this
      // quality. Inline so orbitalselectiontest need not link orbitalwidget.cpp.
      OrbitalQuality selectedQuality() {
        return OrbitalQuality(ui.combo_quality->currentIndex());
      };

      bool precalcLimit() {return m_precalc_limit;}
      int precalcRange() {return m_precalc_range;}

      static double OrbitalQualityToDouble(OrbitalQuality q);
      static double OrbitalQualityToDouble(int i) {
        return OrbitalQualityToDouble(OrbitalQuality(i));};

    public slots:
      void readSettings();
      void writeSettings();
      void reject();
      void fillTable(QList<Orbital> list);
      void setQuality(OrbitalQuality q);
      void selectOrbital(unsigned int orbital);
      void setDefaults(OrbitalWidget::OrbitalQuality quality, double isovalue, bool HOMOFirst);
      void setPrecalcSettings(bool limit, int range);
      void initializeProgress(int orbital, int min, int max, int stage, int totalStages);
      void nextProgressStage(int orbital, int newmin, int newmax);
      void updateProgress(int orbital, int current);
      void calculationComplete(int orbital);
      void calculationQueued(int orbital);

    signals:
      void orbitalSelected(unsigned int orbital);
      void renderRequested(unsigned int orbital, double resolution);
      void calculateAll();

    private slots:
      void tableClicked(const QItemSelection&);
      void renderClicked();
      void configureClicked();

    private:
      Ui::OrbitalWidget ui;
      OrbitalSettingsDialog *m_settings;
      OrbitalQuality m_quality;
      double m_isovalue;

      bool m_precalc_limit;
      int m_precalc_range;

      OrbitalTableModel *m_tableModel;
      OrbitalSortingProxyModel *m_sortedTableModel;
  };
}

#endif
