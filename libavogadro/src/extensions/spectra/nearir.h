#ifndef SPECTRATYPE_NEARIR_H
#define SPECTRATYPE_NEARIR_H

#include "abstract_ir.h"

namespace Avogadro {

class NearIRSpectra : public AbstractIRSpectra
{
    Q_OBJECT

public:
    NearIRSpectra( SpectraDialog *parent = 0 );
    ~NearIRSpectra();

    void writeSettings();
    void readSettings();

    bool checkForData(Molecule* mol);
    void setupPlot(PlotWidget * plot);

    void getCalculatedPlotObject(PlotObject *plotObject);
    void setImportedData(const QList<double> & xList, const QList<double> & yList);
    QString getTSV();
    QString getDataStream(PlotObject *plotObject);
};
}
#endif // SPECTRATYPE_NEARIR_H
