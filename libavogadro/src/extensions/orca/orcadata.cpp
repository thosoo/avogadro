/**********************************************************************
  OrcaData - Data Class Functions

  Copyright (C) 2014 Dagmar Lenk

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 **********************************************************************/

#include <QDialog>
#include <QtCore/QSettings>
#include <QtDebug>
#include "orcadata.h"

#include <avogadro/molecule.h>
#include <avogadro/atom.h>

#include <openbabel/mol.h>


#include <QString>
#include <QTextStream>

using namespace Avogadro;

OrcaVibrations::OrcaVibrations ()
{
    m_dataOK = false;

    m_frequencies.resize(0);
    m_modes.resize(0);
    m_intensities.resize(0);
    m_raman.resize(0);
    m_displacement.resize(0);
}
OrcaVibrations::~OrcaVibrations()
{

}
void OrcaVibrations::setFrequencies(std::vector<double> freq)
{
        m_frequencies = freq;
}
std::vector<double> OrcaVibrations::frequencies()
{
    return m_frequencies;
}

void OrcaVibrations::setIntensities(std::vector<double> intens)
{
        m_intensities = intens;
}
std::vector<double> OrcaVibrations::intensities()
{
    return m_intensities;
}

void OrcaVibrations::setRaman(std::vector<double> raman)
{
        m_raman = raman;
}
std::vector<double> OrcaVibrations::raman()
{
    return m_raman;
}

void OrcaVibrations::setModes(std::vector<int> mode)
{
    m_modes = mode;
}
std::vector<int> OrcaVibrations::modes()
{
    return m_modes;
}

void OrcaVibrations::setDisplacement (std::vector<std::vector<Eigen::Vector3d> *> vec)
{
    m_displacement.resize(0);
    for (unsigned int i = 0; i < vec.size(); ++i) {
        m_displacement.push_back(vec[i]);
    }
}

const std::vector<std::vector<Eigen::Vector3d> *>& OrcaVibrations::displacement() const
{
    return (m_displacement);
}


OrcaBasicData::OrcaBasicData ()
{
    m_calculationType = SP;
    m_methodType = HF;
    m_basisType = OrcaExtension::TZVP;
    m_multiplicity = 1;
    m_charge = 0;
    m_coordsType = CARTESIAN;
}
void OrcaBasicData::reset ()
{
    m_calculationType = SP;
    m_methodType = HF;
    m_basisType = OrcaExtension::TZVP;
    m_multiplicity = 1;
    m_charge = 0;
    m_coordsType = CARTESIAN;
}
void OrcaBasicData::setComment(QString comment)
{
    m_comment = comment;
}
QString OrcaBasicData::getComment()
{
    return m_comment;
}


QString OrcaBasicData::getCalculationTxt()
{
    // Translate the enum calculation Type to normal text

    switch (m_calculationType)
      {
      case SP:
        return "SP";
      case OPT:
        return "OPT";
      case FREQ:
        return "FREQ";
      case OPTFREQ:
        return "OPT FREQ";
      default:
        return "";
      }
}


QString OrcaBasicData::getBasisTxt()
{
    // Translate the enum basis set to normal text
    // enum basisType {SVP, TZVP, TZVPP, QZVPP }
    QString returnBasis = m_enumBasis.valueToKey(m_basisType);
    returnBasis.replace("SV_P", "SV(P)");
    returnBasis.replace("TZVP_F", "TZVP(-f)");
    returnBasis.prepend("def2-");
    return returnBasis;
//    switch (m_basisType)
//    {
//    case SVP:
//        return "def2-SVP";
//    case TZVP:
//        return "def2-TZVP";
//    case TZVPP:
//        return "def2-TZVPP";
//    case QZVPP:
//        return "def2-QZVPP";
//    default:
//        return "";
//    }
}

QString OrcaBasicData::getFormatTxt()
{
    switch (m_coordsType)
    {
    case CARTESIAN:
        return "* xyz";
    case INTERNAL_COORDS:
        return "* int";
    case ZMATRIX_COMPACT:
        return "* gzmt";
    default:
        return "";
    }
}

OrcaBasisData::OrcaBasisData()
{
     m_basis= OrcaExtension::TZVP;
     m_auxBasis = OrcaExtension::TZVP;
     m_auxCorrBasis = OrcaExtension::TZVP;

//     m_useEPC = false;

//     m_useAuxEPC = false;
//     m_useAuxCorrEPC = false;
//     m_useRel = false;
//     m_useDKH = false;
//     m_rel = ZORA;
//     m_DKHOrder = 0;
}
void OrcaBasisData::reset()
{
     m_basis= OrcaExtension::TZVP;
     m_auxBasis = OrcaExtension::TZVP;
     m_auxCorrBasis = OrcaExtension::TZVP;
//     m_useEPC = false;
//     m_useAuxEPC = false;
//     m_useAuxCorrEPC = false;
//     m_useRel = false;
//     m_useDKH = false;
//     m_rel = ZORA;
//     m_DKHOrder = 0;
}
QString OrcaBasisData::getBasisTxt()
{
    // Translate the enum basis set to normal text
    // enum basisType {SVP, TZVP, TZVPP, QZVPP }

    QString returnBasis = m_enumBasis.valueToKey(m_basis);
    returnBasis.replace("SV_P", "SV(P)");
    returnBasis.replace("TZVP_F", "TZVP(-f)");
    returnBasis.prepend("def2-");
//    if (m_useEPC && !m_useRel) {
//        returnBasis.append("-AE");
//    }
    return returnBasis;

}


QString OrcaBasisData::getAuxBasisTxt()
{
    // Translate the enum basis set to normal text
    // used as auxilary basis
    // enum basisType {SVP, TZVP, TZVPP, QZVPP }

//    QString returnBasis = m_enumBasis.valueToKey(m_auxBasis);
//    returnBasis.prepend("def2-");
//    if (m_useEPC || m_useRel) {
//        returnBasis.append("-AE");
//    }
//    returnBasis.append("/J");
//    return returnBasis;
    return "";
}

QString OrcaBasisData::getAuxCorrBasisTxt()
{
    // Translate the enum basis set to normal text
    // used as correlation auxilary basis
    // enum basisType {SVP, TZVP, TZVPP, QZVPP }

    QString returnBasis = m_enumBasis.valueToKey(m_auxCorrBasis);
    returnBasis.replace("SV_P", "SV(P)");
    returnBasis.replace("TZVP_F", "TZVP(-f)");
    returnBasis.prepend("def2-");
    returnBasis.append("/C");
    return returnBasis;

}

//QString OrcaBasisData::getRelTxt ()
//{

//    switch (m_rel)
//    {
//    case ZORA:
//        return "ZORA";
//    case DKH:
//        return "DKH";
//    default:
//        return "";
//    }
//}
OrcaControlData::OrcaControlData()
{
    m_multiplicity = 1;
    m_charge = 0;
    m_calculationType = SP;
    m_methodType = DFT;
    m_dispersion = DISP_D4;
    m_solvationModel = SOLV_MODEL_NONE;
    m_solvent = SOLV_WATER;
    m_useCpcmAdvanced = false;
    m_useDraco = false;
    m_cpcmEpsilon = 0.0;
    m_cpcmRefrac = 0.0;
    m_cpcmRSolv = 0.0;
    m_cpcmSurfaceType = CPCM_SURFACE_DEFAULT;
    m_nProcs = 1;
    m_maxCore = 0;
    m_useTDDFT = false;
    m_tddftRoots = 10;
    m_useNMR = false;
//    m_useMDCI = false;
}
void OrcaControlData::reset()
{
    m_multiplicity = 1;
    m_charge = 0;
    m_calculationType = SP;
    m_methodType = DFT;
    m_dispersion = DISP_D4;
    m_solvationModel = SOLV_MODEL_NONE;
    m_solvent = SOLV_WATER;
    m_useCpcmAdvanced = false;
    m_useDraco = false;
    m_cpcmEpsilon = 0.0;
    m_cpcmRefrac = 0.0;
    m_cpcmRSolv = 0.0;
    m_cpcmSurfaceType = CPCM_SURFACE_DEFAULT;
    m_nProcs = 1;
    m_maxCore = 0;
    m_useTDDFT = false;
    m_tddftRoots = 10;
    m_useNMR = false;
//    m_useMDCI = false;
}
QString OrcaControlData::getCalculationTxt()
{
    // Translate the enum calculation Type to normal text

    switch (m_calculationType)
      {
      case SP:
        return "SP";
      case OPT:
        return "OPT";
      case FREQ:
        return "FREQ";
      case OPTFREQ:
        return "OPT FREQ";
      default:
        return "";
      }
}
QString OrcaControlData::getDispersionTxt() const
{
    switch (m_dispersion) {
    case DISP_D3BJ: return "D3BJ";
    case DISP_D4: return "D4";
    default: return "";
    }
}
QString OrcaControlData::getSolvationModelTxt() const
{
    switch (m_solvationModel) {
    case SOLV_MODEL_CPCM: return "CPCM";
    case SOLV_MODEL_CPCMC: return "CPCMC";
    case SOLV_MODEL_SMD: return "SMD";
    default: return "";
    }
}

QString OrcaControlData::getSolventNameTxt() const
{
    switch (m_solvent) {
    case SOLV_WATER: return "Water";
    case SOLV_ACETONITRILE: return "Acetonitrile";
    case SOLV_DMSO: return "DMSO";
    case SOLV_CHLOROFORM: return "Chloroform";
    case SOLV_METHANOL: return "Methanol";
    case SOLV_ETHANOL: return "Ethanol";
    case SOLV_TOLUENE: return "Toluene";
    case SOLV_DICHLOROMETHANE: return "Dichloromethane";
    case SOLV_THF: return "THF";
    default: return "";
    }
}

QString OrcaControlData::getSolvationTxt() const
{
    const QString model = getSolvationModelTxt();
    const QString solvent = getSolventNameTxt();
    if (model.isEmpty() || solvent.isEmpty())
        return "";
    return QString("%1(%2)").arg(model, solvent);
}

QString OrcaControlData::getCpcmSurfaceTypeTxt() const
{
    switch (m_cpcmSurfaceType) {
    case CPCM_SURFACE_VDW_GAUSSIAN:
        return "vdw_gaussian";
    case CPCM_SURFACE_GEPOL_SES:
        return "gepol_ses";
    case CPCM_SURFACE_GEPOL_SES_GAUSSIAN:
        return "gepol_ses_gaussian";
    default:
        return "";
    }
}

OrcaDFTData::OrcaDFTData()
{
    m_DFTFuncional = OrcaExtension::PBE0;

}
void OrcaDFTData::reset()
{
    m_DFTFuncional = OrcaExtension::PBE0;

}
QString OrcaDFTData::getDFTFunctionalTxt()
{
    // Translate the enum DFTFunctional Type to normal text
//     enum OrcaExtension::DFTFunctionalType {LDA, BP, BLYP, PW91, B3LYP, B3PW, PBEO, TPSS, TPSSH, M06L};

    return m_enumDFT.valueToKey(m_DFTFuncional);
}

OrcaSCFData::OrcaSCFData()
{
    m_useLevel = false;
    m_levelShift = 0.25;
    m_levelError = 0.001;
    m_useDamping = false;
    m_dampFactor = 0.7;
    m_dampError = 0.1;
    m_conv = DIIS;
    m_2ndConv = SOSCF;

    m_scfType = RKS;
    m_maxIter = 125;
    m_accuracy = NORMALSCF;
}
void OrcaSCFData::reset()
{
    m_useLevel = false;
    m_levelShift = 0.25;
    m_levelError = 0.001;
    m_useDamping = false;
    m_dampFactor = 0.7;
    m_dampError = 0.1;
    m_conv = DIIS;
    m_2ndConv = SOSCF;
    m_scfType = RKS;
    m_maxIter = 125;
    m_accuracy = NORMALSCF;
}
QString OrcaSCFData::getAccuracyTxt()
{
    switch (m_accuracy)
    {
    case NORMALSCF:
        return "NormalSCF";
    case TIGHTSCF:
        return "TightSCF";
    case VERYTIGHTSCF:
        return "VeryTightSCF";
    case EXTREMESCF:
        return "ExtremSCF";
    default:
        return "";
    }
}

QString OrcaSCFData::getTypeTxt()
{
    switch (m_scfType)
    {
    case RKS:
        return "RHF";
    case UKS:
        return "UHF";
    default:
        return "ROHF";
    }
}
OrcaDataData::OrcaDataData()
{
    m_coordsType = CARTESIAN;
    m_printLevel = NORMAL;
    m_basisPrint = false;
    m_MOPrint = false;
}
void OrcaDataData::reset()
{
    m_coordsType = CARTESIAN;
    m_printLevel = NORMAL;
    m_basisPrint = false;
    m_MOPrint = false;
}
QString OrcaDataData::getPrintLevelTxt()
{
    switch (m_printLevel)
    {
    case NOTHING:
        return " ";
    case MINI:
        return "MiniPrint";
    case SMALL:
        return "SmallPrint";
    case NORMAL:
        return "NormalPrint";
    case LARGE:
        return "LargePrint";
    default:
        return "";
    }
}

QString OrcaDataData::getFormatTxt()
{
    switch (m_coordsType)
    {
    case CARTESIAN:
        return "Cartesian";
    case INTERNAL_COORDS:
        return "Internal";
    case ZMATRIX_COMPACT:
        return "z-Matrix compact";
    default:
        return "";
    }
}











