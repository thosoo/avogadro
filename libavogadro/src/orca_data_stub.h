#ifndef ORCA_DATA_STUB_H
#define ORCA_DATA_STUB_H

#ifndef OPENBABEL_HAVE_ORCA
#define OPENBABEL_HAVE_ORCA 0
#endif

#if !OPENBABEL_HAVE_ORCA
#include <openbabel/generic.h>
#include <vector>

namespace OpenBabel {
class OBXrayORCAData : public OBGenericData {
protected:
  bool _bXRayData;
  std::vector<double> _vAbsWavelengths;
  std::vector<double> _vEmWavelengths;
  std::vector<double> _vAbsCombined;
  std::vector<double> _vEmCombined;
  std::vector<double> _vAbsD2;
  std::vector<double> _vAbsM2;
  std::vector<double> _vAbsQ2;
  std::vector<double> _vEmD2;
  std::vector<double> _vEmM2;
  std::vector<double> _vEmQ2;
  std::vector<double> _vAbsEDipole;
  std::vector<double> _vAbsVelocity;
  std::vector<double> _vEmEDipole;
  std::vector<double> _vEmVelocity;
public:
  OBXrayORCAData()
    : OBGenericData("ORCASpectraData", OBGenericDataType::CustomData0),
      _bXRayData(false) {}
  virtual ~OBXrayORCAData() {}
  virtual OBGenericData* Clone(OBBase*) const override { return new OBXrayORCAData(*this); }
  OBXrayORCAData& operator=(const OBXrayORCAData&) = default;
  void SetXRayData(const bool &b){ _bXRayData = b; }
  void SetAbsWavelength(const std::vector<double> &v){ _vAbsWavelengths = v; }
  void SetEmWavelength(const std::vector<double> &v){ _vEmWavelengths = v; }
  void SetAbsEDipole(const std::vector<double> &v){ _vAbsEDipole = v; }
  void SetAbsVelocity(const std::vector<double> &v){ _vAbsVelocity = v; }
  void SetEmEDipole(const std::vector<double> &v){ _vEmEDipole = v; }
  void SetEmVelosity(const std::vector<double> &v){ _vEmVelocity = v; }
  void SetAbsCombined(const std::vector<double> &v){ _vAbsCombined = v; }
  void SetAbsD2(const std::vector<double> &v){ _vAbsD2 = v; }
  void SetAbsM2(const std::vector<double> &v){ _vAbsM2 = v; }
  void SetAbsQ2(const std::vector<double> &v){ _vAbsQ2 = v; }
  void SetEmCombined(const std::vector<double> &v){ _vEmCombined = v; }
  void SetEmD2(const std::vector<double> &v){ _vEmD2 = v; }
  void SetEmM2(const std::vector<double> &v){ _vEmM2 = v; }
  void SetEmQ2(const std::vector<double> &v){ _vEmQ2 = v; }
  bool GetXRayData() const { return _bXRayData; }
  std::vector<double> GetAbsWavelengths() const { return _vAbsWavelengths; }
  std::vector<double> GetEmWavelengths() const { return _vEmWavelengths; }
  std::vector<double> GetAbsEDipole() const { return _vAbsEDipole; }
  std::vector<double> GetEmEDipole() const { return _vEmEDipole; }
  std::vector<double> GetAbsVelocity() const { return _vAbsVelocity; }
  std::vector<double> GetEmVelosity() const { return _vEmVelocity; }
  std::vector<double> GetAbsCombined() const { return _vAbsCombined; }
  std::vector<double> GetAbsD2() const { return _vAbsD2; }
  std::vector<double> GetAbsM2() const { return _vAbsM2; }
  std::vector<double> GetAbsQ2() const { return _vAbsQ2; }
  std::vector<double> GetEmCombined() const { return _vEmCombined; }
  std::vector<double> GetEmD2() const { return _vEmD2; }
  std::vector<double> GetEmM2() const { return _vEmM2; }
  std::vector<double> GetEmQ2() const { return _vEmQ2; }
};

class OBOrcaNearIRData : public OBGenericData {
protected:
  bool _bOrcaNearIRData;
  std::vector<double> _vNearIRFrequencies;
  std::vector<double> _vNearIRIntensities;
public:
  OBOrcaNearIRData()
    : OBGenericData("OrcaNearIRSpectraData", OBGenericDataType::CustomData1),
      _bOrcaNearIRData(false) {}
  virtual ~OBOrcaNearIRData() {}
  virtual OBGenericData* Clone(OBBase*) const override { return new OBOrcaNearIRData(*this); }
  OBOrcaNearIRData& operator=(const OBOrcaNearIRData&) = default;
  void SetNearIRData(const bool &b){ _bOrcaNearIRData = b; }
  void SetFrequencies(const std::vector<double> &v){ _vNearIRFrequencies = v; }
  void SetIntensities(const std::vector<double> &v){ _vNearIRIntensities = v; }
  bool GetNearIRData() const { return _bOrcaNearIRData; }
  std::vector<double> GetFrequencies() const { return _vNearIRFrequencies; }
  std::vector<double> GetIntensities() const { return _vNearIRIntensities; }
};
} // namespace OpenBabel

#endif // !OPENBABEL_HAVE_ORCA
#endif // ORCA_DATA_STUB_H
