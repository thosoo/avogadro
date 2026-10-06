/******************************************************************************

  This source file is part of the OpenQube project.

  Copyright 2014 Dagmar Lenk

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/

#include "orca.h"

#include <QFile>
#include <QStringList>
#include <QString>
#include <QDebug>

#include <avogadro/fragment.h>
#include <QRegularExpression>

using Eigen::Vector3d;
using std::vector;

#ifndef BOHR_TO_ANGSTROM
#define BOHR_TO_ANGSTROM 0.529177249
#endif

using namespace Qt;

namespace OpenQube
{

ORCAOutput::ORCAOutput(const QString &filename, GaussianSet* basis):
    m_coordFactor(1.0), m_currentMode(NotParsing), m_currentAtom(1)
{

    // Open the file for reading and process it
    QFile* file = new QFile(filename);
    file->open(QIODevice::ReadOnly | QIODevice::Text);
    m_in = file;
    qDebug() << "File" << filename << "opened.";

    m_openShell = false;
    m_useBeta = false;
    m_orcaSuccess = true;

    basis->setUseOrcaNormalization(true);

    //  openShellButton = qobject_cast<QCheckButton *>( m_scfConv2ndButtons->button(scfData->getConv2nd()) );

    // Process the formatted checkpoint and extract all the information we need
    while (!m_in->atEnd()) {
        processLine(basis);
    }

    // Now it should all be loaded load it into the basis set - BUT better check it

    if (!m_orcaSuccess || m_electrons == 0 || m_MOcoeffs.size() == 0 || m_shellNums.size() == 0) {
        m_electrons = 0;
        m_orcaSuccess = false;
    } else {
        load(basis);
    }
    delete file;
}

ORCAOutput::~ORCAOutput()
{
    clearBasisFunctions();
}

void ORCAOutput::clearBasisFunctions()
{
    // N.5/5.1: m_basisFunctions is std::vector<std::vector<std::vector<...> *> *>,
    // i.e. a vector of heap vectors, each of which owns heap vectors. Free both
    // pointer levels before clearing so a fresh parse does not leak the tree.
    for (std::vector<std::vector<std::vector<Eigen::Vector2d> *> *>::iterator
             it = m_basisFunctions.begin(),
             end = m_basisFunctions.end();
         it != end; ++it) {
        std::vector<std::vector<Eigen::Vector2d> *> *atomVec = *it;
        if (atomVec) {
            for (std::vector<std::vector<Eigen::Vector2d> *>::iterator
                     it2 = atomVec->begin(), end2 = atomVec->end();
                 it2 != end2; ++it2) {
                delete *it2;
            }
            delete atomVec;
        }
    }
    m_basisFunctions.clear();
    m_basisFunctions.resize(0);
}

void ORCAOutput::processLine(GaussianSet *basis)
{
    // First truncate the line, remove trailing white space and check for blank lines
    QString key = m_in->readLine().trimmed();
    while(key.isEmpty() && !m_in->atEnd()) {
        key = m_in->readLine().trimmed();
    }

    if (m_in->atEnd())
        return;

    QStringList list = key.split(' ', Qt::SkipEmptyParts);
    int numGTOs;

    // Big switch statement checking for various things we are interested in
    // Make sure to switch mode:
    //      enum mode { NotParsing, Atoms, GTO, STO, MO, SCF }
    if (key.contains("CARTESIAN COORDINATES (A.U.)", Qt::CaseInsensitive)) {
        basis->moleculeRef().clearAtoms();

        m_coordFactor = 1.; // leave the coords in BOHR ....
        m_currentMode = Atoms;
        m_atomLabel.clear();
        key = m_in->readLine(); // skip ----- line
        key = m_in->readLine(); // column titles
    } else if (key.contains("BASIS SET INFORMATION")) {
        if (!key.contains("AUXILIARY")) { // skip auxiliary basis set infos
            m_currentMode = GTO;
            key = m_in->readLine(); // skip ----- line

            // Number of groups of distinct atoms
            key = m_in->readLine();
            list = key.split(' ', Qt::SkipEmptyParts);
            if (list.size() > 3) {
                m_nGroups = list[2].toInt();
            } else {
                qDebug() << "Something wrong with the basisset informations";
                return;
            }
            key = m_in->readLine(); // skip blank line
            for (int i = 0; i < m_nGroups; ++i) {
                key = m_in->readLine();             //skip group information
            }
            key = m_in->readLine(); // skip blank line
            for (uint i = 0; i < basis->moleculeRef().numAtoms(); ++i) {
                key = m_in->readLine();             //skip group information
            }

            // now skip
            // blank line
            // ----------------------------
            // # Basis set for element : x
            // ----------------------------
            // blank line
            for (unsigned int i = 0; i < 6; ++i) {
                key = m_in->readLine();
            }
        }
    } else if (key.contains("TOTAL NUMBER OF BASIS SET")) {
        m_currentMode = NotParsing; // no longer reading GTOs
    } else if (key.contains("NUMBER OF CARTESIAN GAUSSIAN BASIS")) {
        m_currentMode = NotParsing; // no longer reading GTOs
    } else if (key.contains("Number of Electrons")) {
        list = key.split(' ', Qt::SkipEmptyParts);
        m_electrons = list[5].toInt();
    } else if (key.contains("SPIN UP ORBITALS") && !m_openShell) {
        // N.5/5.2: record that this is an open-shell calculation but do NOT prompt
        // mid-parse. The alpha block is always parsed into m_MOcoeffs and the beta
        // block unconditionally into m_MOcoeffsBeta; the caller chooses which to
        // export via setUseBeta() after parsing (default alpha). A modal dialog
        // here would block the parse thread and, on a stale document, could touch
        // freed widgets.
        m_openShell = true;
    } else if (key.contains("MOLECULAR ORBITALS")) { //|| key.contains("MOLECULAR ORBITALS")) {
        m_currentMode = MO;
        key = m_in->readLine(); //------------
    } else {

        vector <vector <double> > columns;
        unsigned int numColumns, numRows;
        numColumns = 0;
        numRows = 0;
        // parsing a line -- what mode are we in?

        switch (m_currentMode) {
        case Atoms: {
            if (key.isEmpty()) break;
            list = key.split(' ', Qt::SkipEmptyParts);
            while (!key.isEmpty()){
                if (list.size() < 8) {
                    break;
                }
                Vector3d pos(list[5].toDouble() * m_coordFactor,
                        list[6].toDouble() * m_coordFactor,
                        list[7].toDouble() * m_coordFactor);

                basis->moleculeRef().addAtom(pos, int (list[2].toDouble()));
                m_atomLabel +=list[1].trimmed();
                key = m_in->readLine().trimmed();
                list = key.split(' ', Qt::SkipEmptyParts);
            }
            m_currentMode = NotParsing;
            break;
        }
        case GTO: {
            //            // should start at the first newGTO
            if (key.isEmpty())
                break;
            numGTOs = 0;
            list = key.split(' ', Qt::SkipEmptyParts);
            int nShells;
            // init all vectors etc.
            m_basisAtomLabel.clear();
            m_orcaNumShells.resize(0);
            // N.5/5.1: free the previous basis-function tree before re-sizing so
            // re-entry of this branch does not leak the old heap-allocated tree.
            clearBasisFunctions();
            m_basisFunctions.resize(0);
            m_orcaShellTypes.resize(0);

            m_a.resize(0);
            m_c.resize(0);
            m_shellNums.resize(0);
            m_shellTypes.resize(0);
            m_shelltoAtom.resize(0);
            while (list[0].trimmed() == "NewGTO") {
                m_basisAtomLabel += list[1].trimmed();

                key = m_in->readLine().trimmed();

                list = key.split(' ', Qt::SkipEmptyParts);

                nShells = 0;
                m_basisFunctions.push_back(new std::vector<std::vector<Eigen::Vector2d> *>);
                shellFunctions.resize(0);
                shellTypes.resize(0);
                while (list[0].trimmed() != "end;"){

                    int nFunc = list[1].trimmed().toInt();
                    shellTypes.push_back(orbitalIdx(list[0]));
                    shellFunctions.push_back(nFunc);
                    m_basisFunctions.at(numGTOs)->push_back(new std::vector<Eigen::Vector2d>(nFunc));

                    for (int i=0;i<nFunc;i++) {
                        key = m_in->readLine().trimmed();

                        list = key.split(' ', Qt::SkipEmptyParts);
                        m_basisFunctions.at(numGTOs)->at(nShells)->at(i).x() = list[1].toDouble();          // exponent
                        m_basisFunctions.at(numGTOs)->at(nShells)->at(i).y() = list[2].toDouble();          // coeff
                    }

                    nShells++;
                    key = m_in->readLine().trimmed();

                    list = key.split(' ', Qt::SkipEmptyParts);
                }
                m_orcaShellTypes.push_back(std::vector<orbital>(shellTypes.size()));
                m_orcaShellTypes.at(numGTOs) =  shellTypes;
                m_orcaNumShells.push_back(std::vector<int>(shellFunctions.size()));
                m_orcaNumShells.at(numGTOs) = shellFunctions;
                numGTOs++;
                key = m_in->readLine().trimmed();
                key = m_in->readLine().trimmed();
                key = m_in->readLine().trimmed();

                list = key.split(' ', Qt::SkipEmptyParts);
                if (list.size() == 0) break; // unexpected structure - suppose no more NewGTOs
            }

            // create input for gaussian basisset
            int nBasis = numGTOs;
            int nAtoms = m_atomLabel.size();
            m_currentAtom = 0;
            for (int i=0;i<nAtoms;i++) {
                m_currentAtom++;
                for (int j=0; j<nBasis;j++) {
                    if (m_atomLabel.at(i) == m_basisAtomLabel.at(j)) {
                        for (uint k=0;k<m_orcaNumShells.at(j).size();k++){
                            for (int l=0;l<m_orcaNumShells.at(j).at(k);l++) {
                                m_a.push_back(m_basisFunctions.at(j)->at(k)->at(l).x());
                                m_c.push_back(m_basisFunctions.at(j)->at(k)->at(l).y());
                            }
                            m_shellNums.push_back(m_orcaNumShells.at(j).at(k));
                            m_shellTypes.push_back(m_orcaShellTypes.at(j).at(k));
                            m_shelltoAtom.push_back(m_currentAtom);
                        }
                        break;
                    }
                }
            }
            m_currentMode = NotParsing;
            break;
        }
        case MO: {

            m_MOcoeffs.clear(); // if the orbitals were punched multiple times
            QStringList orcaOrbitals;

            while(!key.trimmed().isEmpty()) {
                // currently reading the sequence number
                key = m_in->readLine(); // energies
                key = m_in->readLine(); // symmetries
                key = m_in->readLine(); // skip -----------
                key = m_in->readLine(); // now we've got coefficients

                QRegularExpression rx("[.][0-9]{6}[0-9-]");
                while (true){          // avoid wrong splitting
                    const QRegularExpressionMatch match = rx.match(key);
                    if (!match.hasMatch())
                        break;
                    key.insert(match.capturedStart()+1, " ");
                }
                list = key.split(' ', Qt::SkipEmptyParts);

                numColumns = list.size() - 2;
                columns.resize(numColumns);
                while (list.size() > 2) {
                    orcaOrbitals += list[1];
                    for (unsigned int i = 0; i < numColumns; ++i) {
                        columns[i].push_back(list[i + 2].toDouble());
                    }

                    key = m_in->readLine();
                    while (true){          // avoid wrong splitting
                        const QRegularExpressionMatch match = rx.match(key);
                        if (!match.hasMatch())
                            break;
                        key.insert(match.capturedStart()+1, " ");
                    }

                    list = key.split(' ', Qt::SkipEmptyParts);
                    if (list.size() != numColumns+2)
                        break;

                } // ok, we've finished one batch of MO coeffs
                // now reorder the p orbitals from "orcaStyle" (pz, px,py) to expected (px,py,pz)
                int idx = 0;
                while (idx < (int)orcaOrbitals.size()){
                    if (orcaOrbitals.at(idx).contains("pz")) {
                        // N.5/5.3: the reorder swaps (idx, idx+1) then (idx+1, idx+2),
                        // so the triple idx, idx+1, idx+2 must all be valid in the
                        // orbital list AND in every column; a truncated MO block would
                        // let idx+2 run off the end of an at() call.
                        if (idx + 2 >= (int)orcaOrbitals.size())
                            break;
                        bool columnsTruncated = false;
                        for (uint i = 0; i < numColumns; i++) {
                            if (idx + 2 >= (int)columns[i].size()) {
                                columnsTruncated = true;
                                break;
                            }
                        }
                        if (columnsTruncated)
                            break;
                        for (uint i=0;i<numColumns;i++){
                            qSwap (columns[i].at(idx),columns[i].at(idx+1));
                        }
                        idx++;
                        for (uint i=0;i<numColumns;i++){
                            qSwap (columns[i].at(idx),columns[i].at(idx+1));
                        }
                        idx++;
                        idx++;
                    } else {
                        idx++;
                    }
                }

                // Now we need to re-order the MO coeffs, so we insert one MO at a time
                for (unsigned int i = 0; i < numColumns; ++i) {
                    numRows = columns[i].size();
                    for (unsigned int j = 0; j < numRows; ++j) {
                        m_MOcoeffs.push_back(columns[i][j]);
                    }
                }
                columns.clear();
                orcaOrbitals.clear();

            } // finished parsing MOs
            if (m_MOcoeffs.size() != numRows*numRows) {
                m_orcaSuccess = false;
                qDebug() << "Something went wrong during read of MOs\n check columns!!!";
            }
            m_numBasisFunctions = numRows;
            // N.5/5.2: for open-shell files parse the beta block unconditionally
            // into m_MOcoeffsBeta; the alpha block already populated m_MOcoeffs.
            // The caller (load) picks which to export via setUseBeta() (default
            // alpha), so no modal dialog is needed mid-parse.
            if (m_openShell) {
                m_MOcoeffsBeta.clear(); // if the orbitals were punched multiple times
                QStringList orcaOrbitals;
                key = m_in->readLine();
                while(!key.trimmed().isEmpty()) {
                    // currently reading the sequence number
                    key = m_in->readLine(); // energies
                    key = m_in->readLine(); // symmetries
                    key = m_in->readLine(); // skip -----------
                    key = m_in->readLine(); // now we've got coefficients

                QRegularExpression rx("[.][0-9]{6}[0-9-]");
                    while (true){          // avoid wrong splitting
                        const QRegularExpressionMatch match = rx.match(key);
                        if (!match.hasMatch())
                            break;
                        key.insert(match.capturedStart()+1, " ");
                    }
                    list = key.split(' ', Qt::SkipEmptyParts);
                    numColumns = list.size() - 2;
                    columns.resize(numColumns);
                    while (list.size() > 2) {
                        orcaOrbitals += list[1];
                        //                    columns.resize(numColumns);
                        for (unsigned int i = 0; i < numColumns; ++i) {
                            columns[i].push_back(list[i + 2].toDouble());
                        }

                        key = m_in->readLine();
                        while (true){          // avoid wrong splitting
                            const QRegularExpressionMatch match = rx.match(key);
                            if (!match.hasMatch())
                                break;
                            key.insert(match.capturedStart()+1, " ");
                        }
                        list = key.split(' ', Qt::SkipEmptyParts);
                        if (list.size() != numColumns+2)
                            break;

                    } // ok, we've finished one batch of MO coeffs
                    // now reorder the p orbitals from "orcaStyle" (pz, px,py) to expected (px,py,pz)
                    int idx = 0;
                    while (idx < (int)orcaOrbitals.size()){
                        if (orcaOrbitals.at(idx).contains("pz")) {
                            // N.5/5.3: guard the trailing at(idx+1) swap — the reorder
                            // touches idx, idx+1, idx+2, so all three must be valid in
                            // the orbital list AND in every column.
                            if (idx + 2 >= (int)orcaOrbitals.size())
                                break;
                            bool columnsTruncated = false;
                            for (uint i = 0; i < numColumns; i++) {
                                if (idx + 2 >= (int)columns[i].size()) {
                                    columnsTruncated = true;
                                    break;
                                }
                            }
                            if (columnsTruncated)
                                break;
                            for (uint i=0;i<numColumns;i++){
                                qSwap (columns[i].at(idx),columns[i].at(idx+1));
                            }
                            idx++;
                            for (uint i=0;i<numColumns;i++){
                                qSwap (columns[i].at(idx),columns[i].at(idx+1));
                            }
                            idx++;
                            idx++;
                        } else {
                            idx++;
                        }
                    }

                    // Now we need to re-order the MO coeffs, so we insert one MO at a time
                    for (unsigned int i = 0; i < numColumns; ++i) {
                        numRows = columns[i].size();
                        for (unsigned int j = 0; j < numRows; ++j) {

                            m_MOcoeffsBeta.push_back(columns[i][j]);
                        }
                    }
                    columns.clear();
                    orcaOrbitals.clear();

                    if (key.trimmed().isEmpty())
                        key = m_in->readLine(); // skip the blank line after the MOs
                } // finished parsing 2nd. MOs
                if (m_MOcoeffsBeta.size() != numRows*numRows) {
                    m_orcaSuccess = false;
                    qDebug() << "Something went wrong during read of MOs\n check columns!!!";
                }
                m_numBasisFunctions = numRows;
            }

            m_currentMode = NotParsing;
            break;
        }
        default:
            ;
        } // end switch
    } // end if (mode)
} // end process line

void ORCAOutput::load(GaussianSet* basis)
{
  // Now load up our basis set
  basis->setNumElectrons(m_electrons);

  // Set up the GTO primitive counter, go through the shells and add them
  int nGTO = 0;
  int nSP = 0; // number of SP shells
  for (unsigned int i = 0; i < m_shellTypes.size(); ++i) {
    // Handle the SP case separately - this should possibly be a distinct type
    if (m_shellTypes.at(i) == SP)  {
      // SP orbital type - currently have to unroll into two shells
      int tmpGTO = nGTO;
      int s = basis->addBasis(m_shelltoAtom.at(i) - 1, S);
      for (int j = 0; j < m_shellNums.at(i); ++j) {
        basis->addGTO(s, m_c.at(nGTO), m_a.at(nGTO));
        ++nGTO;
      }
      int p = basis->addBasis(m_shelltoAtom.at(i) - 1, P);
      for (int j = 0; j < m_shellNums.at(i); ++j) {
        basis->addGTO(p, m_csp.at(nSP), m_a.at(tmpGTO));
        ++tmpGTO;
        ++nSP;
      }
    }
    else {
      int b = basis->addBasis(m_shelltoAtom.at(i) - 1, m_shellTypes.at(i));
      for (int j = 0; j < m_shellNums.at(i); ++j) {
        basis->addGTO(b, m_c.at(nGTO), m_a.at(nGTO));
        ++nGTO;
      }
    }
  }

  // Now to load in the MO coefficients. For open-shell files both the alpha
  // block (m_MOcoeffs) and the beta block (m_MOcoeffsBeta) were parsed; pick
  // the one the caller requested via setUseBeta() (default alpha).
  const std::vector<double> *coeffs = &m_MOcoeffs;
  if (m_useBeta && !m_MOcoeffsBeta.empty())
      coeffs = &m_MOcoeffsBeta;
  if (coeffs->size())
      basis->addMOs(*coeffs);

  // N.5/5.3: clamp HOMO into [1, m_numBasisFunctions] so calculateDensityMatrix
  // never iterates k past the end of moMatrix.
  m_homo = (int)ceil(m_electrons / 2.0);
  if (m_homo < 1)
    m_homo = 1;
  if (m_homo > (int)m_numBasisFunctions)
    m_homo = (int)m_numBasisFunctions;
  calculateDensityMatrix();

  basis->setDensityMatrix(m_density);

  qDebug() << " Orca loadBasis done";
}

void ORCAOutput::outputAll()
{
  qDebug() << "Shell mappings.";
  for (unsigned int i = 0; i < m_shellTypes.size(); ++i)
    qDebug() << i << ": type =" << m_shellTypes.at(i)
             << ", number =" << m_shellNums.at(i)
             << ", atom =" << m_shelltoAtom.at(i);
  qDebug() << "MO coefficients.";
  for (unsigned int i = 0; i < m_MOcoeffs.size(); ++i)
    qDebug() << m_MOcoeffs.at(i);
}

orbital ORCAOutput::orbitalIdx(QString txt) {
    if (txt == "S") return S;
    if (txt == "SP") return SP;
    if (txt == "P") return P;
    //    if (txt == "D") return D;
    if (txt == "D") return D5;  //// orca only uses Spherical - 5 d components
    if (txt == "D5") return D5;
    if (txt == "F") return F7;  //// orca only uses Spherical - 7 f components
    if (txt == "F7") return F7;
    if (txt == "G") return G9;  //// orca only uses Spherical - 9 g components
    if (txt == "G9") return G9;
    if (txt == "H") return H11; //// orca only uses Spherical - 11 h components
    if (txt == "H11") return H11;
    if (txt == "I") return I13; //// orca only uses Spherical - 13 i components
    if (txt == "I13") return I13;
    return UU;
}

void ORCAOutput::calculateDensityMatrix()
{
    // N.5/5.3: the flat coefficient index reaches m_numBasisFunctions^2 - 1; if
    // we have fewer coefficients (truncated / MO-missed file) bail before the
    // out-of-bounds read below rather than corrupting the density matrix.
    if (m_MOcoeffs.size() < (size_t)m_numBasisFunctions * (size_t)m_numBasisFunctions) {
        m_orcaSuccess = false;
        return;
    }

    std::vector<std::vector<double> > dens;
//    dens.resize(m_numBasisFunctions, vector<double>(m_numBasisFunctions, 0.0));

    Eigen::MatrixXd moMatrix;
    moMatrix.resize(m_numBasisFunctions,m_numBasisFunctions);
    m_density.resize(m_numBasisFunctions,m_numBasisFunctions);


    for (unsigned int j = 0; j < m_numBasisFunctions; ++j)
        for (unsigned int i = 0; i < m_numBasisFunctions; ++i)
            moMatrix.coeffRef(i, j) = m_MOcoeffs[i + j*m_numBasisFunctions];

    for (int j = 0; j < m_numBasisFunctions; j++) {
        for (int i = 0; i< m_numBasisFunctions; i++) {
            m_density(i,j) = 0.;
            double density = 0.;
            for (int k = 0; k < m_homo; k++) {
                 density += moMatrix.coeffRef(i,k)*moMatrix.coeffRef(j,k);
//                 dens.at(i).at(j) += m_MOcoeffs[k + i*m_numBasisFunctions] * m_MOcoeffs[k + j*m_numBasisFunctions];
            }
            m_density(i,j) = 2*density;
//                 m_density(i,j) += moMatrix.coeffRef(i,50)*moMatrix.coeffRef(j,50);

 //           qDebug() << " (" << i << "," << j << ") = " << moMatrix.coeffRef (i,j) << "  " << dens.at(i).at(j) << "  " <<  m_density(i,j);
        }
    }
}

} // end OpenQube namespace

