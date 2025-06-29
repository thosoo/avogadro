#ifndef OBORCA_STUB_H
#define OBORCA_STUB_H
#include <openbabel/generic.h>
#include <vector>
namespace OpenBabel {
class OBOrcaSpecData : public OBGenericData {
public:
  OBOrcaSpecData()
    : OBGenericData("OrcaSpectraData", OBGenericDataType::CustomData0) {}
  OBGenericData *Clone(OBBase *parent) const override {
    return new OBOrcaSpecData(*this);
  }
  bool GetSpecData() const { return !m_absWavelengths.empty(); }
  const std::vector<double> &GetAbsWavelengths() const { return m_absWavelengths; }
  const std::vector<double> &GetAbsEDipole() const { return m_absEDipole; }
  const std::vector<double> &GetEmWavelengths() const { return m_emWavelengths; }
  const std::vector<double> &GetEmEDipole() const { return m_emEDipole; }
  void SetAbsWavelengths(const std::vector<double> &v) { m_absWavelengths = v; }
  void SetAbsEDipole(const std::vector<double> &v) { m_absEDipole = v; }
  void SetEmWavelengths(const std::vector<double> &v) { m_emWavelengths = v; }
  void SetEmEDipole(const std::vector<double> &v) { m_emEDipole = v; }
private:
  std::vector<double> m_absWavelengths;
  std::vector<double> m_absEDipole;
  std::vector<double> m_emWavelengths;
  std::vector<double> m_emEDipole;
};

class OBOrcaNearIRData : public OBGenericData {
public:
  OBOrcaNearIRData()
    : OBGenericData("OrcaNearIRSpectraData", OBGenericDataType::CustomData1),
      m_hasData(false) {}
  OBGenericData *Clone(OBBase *parent) const override {
    return new OBOrcaNearIRData(*this);
  }
  bool GetNearIRData() const { return m_hasData; }
  void SetNearIRData(bool b) { m_hasData = b; }
  const std::vector<double> &GetFrequencies() const { return m_freq; }
  const std::vector<double> &GetIntensities() const { return m_intensity; }
  void SetFrequencies(const std::vector<double> &v) { m_freq = v; }
  void SetIntensities(const std::vector<double> &v) { m_intensity = v; }
private:
  bool m_hasData;
  std::vector<double> m_freq;
  std::vector<double> m_intensity;
};
}
#endif
