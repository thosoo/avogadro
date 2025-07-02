#include "nearir.h"
#include <QtWidgets/QMessageBox>
#include <QtCore/QDebug>

#include <openbabel/mol.h>
#include <openbabel/generic.h>

#include <vector>

using namespace std;

namespace Avogadro {

NearIRSpectra::NearIRSpectra(SpectraDialog *parent ) :
    AbstractIRSpectra( parent )
{
    ui.group_ramanIntensities->hide();
//    ui.combo_yaxis->addItem(tr("Transmittance (%)"));
//    ui.combo_yaxis->addItem(tr("Absorbance (%)"));
    ui.combo_yaxis->setCurrentIndex(1);
    readSettings();
}
NearIRSpectra::~NearIRSpectra() {
    // TODO: Anything to delete?
    writeSettings();
}
void NearIRSpectra::readSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    m_scale = settings.value("spectra/NearIR/scale", 1.0).toDouble();
    ui.spin_scale->setValue(m_scale);
    updateScaleSlider(m_scale);
    m_fwhm = settings.value("spectra/NearIR/gaussianWidth",0.0).toDouble();
    ui.spin_FWHM->setValue(m_fwhm);
    updateFWHMSlider(m_fwhm);
    ui.cb_labelPeaks->setChecked(settings.value("spectra/NearIR/labelPeaks",false).toBool());
    QString yunit = settings.value("spectra/NearIR/yAxisUnits",tr("Absorbance (%)")).toString();
    updateYAxis(yunit);
    if (yunit == "Transmittance (%)")
        ui.combo_yaxis->setCurrentIndex(0);

    ui.combo_lineShape->setCurrentIndex(settings.value("spectra/NearIR/lineShape", GAUSSIAN).toInt());
    m_lineShape = LineShape(ui.combo_lineShape->currentIndex());
    ui.spin_nPoints->setValue(settings.value("spectra/NearIR/nPoints",10).toInt());
    emit plotDataChanged();
}
void NearIRSpectra::writeSettings()
{
    QSettings settings; // Already set up in avogadro/src/main.cpp

    settings.setValue("spectra/NearIR/scale", m_scale);
    settings.setValue("spectra/NearIR/gaussianWidth", m_fwhm);
    settings.setValue("spectra/NearIR/labelPeaks", ui.cb_labelPeaks->isChecked());
    settings.setValue("spectra/NearIR/yAxisUnits", ui.combo_yaxis->currentText());
    settings.setValue("spectra/NearIR/lineShape", ui.combo_lineShape->currentIndex());
    settings.setValue("spectra/NearIR/nPoints", ui.spin_nPoints->value());
}

bool NearIRSpectra::checkForData(Molecule * mol) {
    OpenBabel::OBMol obmol = mol->OBMol();
//    OpenBabel::OBVibrationData *vibrations = static_cast<OpenBabel::OBVibrationData*>(obmol.GetData(OpenBabel::OBGenericDataType::VibrationData));
    OpenBabel::OBOrcaNearIRData *ond = static_cast<OpenBabel::OBOrcaNearIRData*>(obmol.GetData("OrcaNearIRSpectraData"));

    if (!ond) return false;
    if (!ond->GetNearIRData()) return false;

    // OK, we have valid vibrations, so store them for later
    vector<double> wavenumbers = ond->GetFrequencies();
    vector<double> intensities = ond->GetIntensities();

    // Case where there are no intensities, set all intensities to an arbitrary value, i.e. 1.0
    if (wavenumbers.size() > 0 && intensities.size() == 0) {
        // Warn user
        QMessageBox::information(m_dialog, tr("No intensities"), tr("The vibration data in the molecule you have loaded does not have any intensity data. Intensities have been set to an arbitrary value for visualization."));
        for (uint i = 0; i < wavenumbers.size(); i++) {
            intensities.push_back(1.0);
        }
    }

    // Normalize intensities into transmittances
    double maxIntensity=0;
    for (unsigned int i = 0; i < intensities.size(); i++) {
        if (intensities.at(i) >= maxIntensity) {
            maxIntensity = intensities.at(i);
        }
    }

    vector<double> absorbances;

    for (unsigned int i = 0; i < intensities.size(); i++) {
        double t = intensities.at(i);
        if (maxIntensity != 0) {
            t = t / maxIntensity; 	// Normalize
        }
        t *= 100.0;		// Convert to percent
        absorbances.push_back(t);
    }

    // Store in member vars
    m_xList.clear();
    m_xList_orig.clear();
    m_yList.clear();
    for (uint i = 0; i < wavenumbers.size(); i++){
        double w = wavenumbers.at(i);
        m_xList.append(w*scale(w));
        m_xList_orig.append(w);
        m_yList.append(absorbances.at(i));
    }

    return true;
}
void NearIRSpectra::setupPlot(PlotWidget * plot) {
  plot->setDefaultLimits( 3500.0, 400.0, 0.0, 100.0 );
  plot->axis(PlotWidget::BottomAxis)->setLabel(tr("Wavenumber (cm<sup>-1</sup>)"));
  plot->axis(PlotWidget::LeftAxis)->setLabel(m_yaxis);
}

void NearIRSpectra::getCalculatedPlotObject(PlotObject *plotObject) {
  AbstractIRSpectra::getCalculatedPlotObject(plotObject);
  // Convert to transmittance?
  if (ui.combo_yaxis->currentIndex() == 0) {
    for(int i = 0; i< plotObject->points().size(); i++) {
      double transmittance = 100 - plotObject->points().at(i)->y();
      plotObject->points().at(i)->setY(transmittance);
    }
  }
  // Add labels for gaussians?
  if ((m_fwhm != 0.0) && (ui.cb_labelPeaks->isChecked())) {
    if (ui.combo_yaxis->currentIndex() == 1) {
      assignGaussianLabels(plotObject, true, m_labelYThreshold);
      m_dialog->labelsUp(true);
    } else {
      assignGaussianLabels(plotObject, false, 100-m_labelYThreshold);
      m_dialog->labelsUp(false);
    }
  }
}


void NearIRSpectra::setImportedData(const QList<double> & xList, const QList<double> & yList) {
  m_xList_imp = xList;
  m_yList_imp = yList;

  // Convert y values to percents from fraction, if necessary...
  bool convert = true;
  for (int i = 0; i < m_yList_imp.size(); i++) {
    if (m_yList_imp.at(i) > 1.5) { // If transmittances exist greater than this, they're already in percent.
      convert = false;
      break;
    }
  }
  if (convert) {
    for (int i = 0; i < m_yList_imp.size(); i++) {
      double tmp = m_yList_imp.at(i);
      tmp *= 100;
      m_yList_imp.replace(i, tmp);
    }
  }
}

QString NearIRSpectra::getTSV() {
  return SpectraType::getTSV("Frequencies", "Intensities");
}

QString NearIRSpectra::getDataStream(PlotObject *plotObject)
{
    return SpectraType::getDataStream (plotObject, "Frequencies", "Intensities");
}
} // namespace Avogadro
