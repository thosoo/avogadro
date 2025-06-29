#ifndef AVOGADRO_OBORCANEARIR_STUB_H
#define AVOGADRO_OBORCANEARIR_STUB_H

#include <openbabel/generic.h>

#ifndef OBORCANEARIRDATA_DEFINED
namespace OpenBabel {
class OBOrcaNearIRData : public OBGenericData
{
  bool _bOrcaNearIRData = false;
  std::vector<double> _vNearIRFrequencies;
  std::vector<double> _vNearIRIntensities;
public:
  OBOrcaNearIRData() : OBGenericData("OrcaNearIRSpectraData", OBGenericDataType::CustomData1) {}
  OBGenericData* Clone(OBBase*) const override { return new OBOrcaNearIRData(*this); }
  OBOrcaNearIRData& operator=(const OBOrcaNearIRData&) = default;
  void SetNearIRData(const bool& b) { _bOrcaNearIRData = b; }
  void SetFrequencies(const std::vector<double>& v) { _vNearIRFrequencies = v; }
  void SetIntensities(const std::vector<double>& v) { _vNearIRIntensities = v; }
  bool GetNearIRData() const { return _bOrcaNearIRData; }
  std::vector<double> GetFrequencies() const { return _vNearIRFrequencies; }
  std::vector<double> GetIntensities() const { return _vNearIRIntensities; }
};
#define OBORCANEARIRDATA_DEFINED
#endif

#ifndef OBORCASPECDATA_DEFINED
class OBOrcaSpecData : public OBGenericData
{
  bool _bOrcaSpecData = false;
  std::vector<double> _vFrequencies;
  std::vector<double> _vIntensities;
public:
  OBOrcaSpecData() : OBGenericData("OrcaSpectraData", OBGenericDataType::CustomData0) {}
  OBGenericData* Clone(OBBase*) const override { return new OBOrcaSpecData(*this); }
  OBOrcaSpecData& operator=(const OBOrcaSpecData&) = default;
  void SetOrcaSpectraData(const bool& b) { _bOrcaSpecData = b; }
  void SetFrequencies(const std::vector<double>& v) { _vFrequencies = v; }
  void SetIntensities(const std::vector<double>& v) { _vIntensities = v; }
  bool GetOrcaSpectraData() const { return _bOrcaSpecData; }
  std::vector<double> GetFrequencies() const { return _vFrequencies; }
  std::vector<double> GetIntensities() const { return _vIntensities; }
};
#define OBORCASPECDATA_DEFINED
#endif

} // namespace OpenBabel

#endif // AVOGADRO_OBORCANEARIR_STUB_H
