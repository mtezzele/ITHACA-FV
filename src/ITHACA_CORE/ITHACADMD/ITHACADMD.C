/*---------------------------------------------------------------------------*\
     ██╗████████╗██╗  ██╗ █████╗  ██████╗ █████╗       ███████╗██╗   ██╗
     ██║╚══██╔══╝██║  ██║██╔══██╗██╔════╝██╔══██╗      ██╔════╝██║   ██║
     ██║   ██║   ███████║███████║██║     ███████║█████╗█████╗  ██║   ██║
     ██║   ██║   ██╔══██║██╔══██║██║     ██╔══██║╚════╝██╔══╝  ╚██╗ ██╔╝
     ██║   ██║   ██║  ██║██║  ██║╚██████╗██║  ██║      ██║      ╚████╔╝
     ╚═╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝      ╚═╝       ╚═══╝

 * In real Time Highly Advanced Computational Applications for Finite Volumes
 * Copyright (C) 2017 by the ITHACA-FV authors
-------------------------------------------------------------------------------

  License
  This file is part of ITHACA-FV

  ITHACA-FV is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  ITHACA-FV is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with ITHACA-FV. If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

/// \file
/// source file for the ITHACADMD class

#include "ITHACADMD.H"

template<class FieldType>
ITHACADMD<FieldType>::ITHACADMD(
    PtrList<GeometricField<FieldType, fvPatchField, volMesh>>& snapshots, double dt)
    :
    snapshotsDMD(snapshots),
    NSnaps(snapshots.size()),
    originalDT(dt)
{
    ITHACAparameters para;
    redSVD = para.ITHACAdict->lookupOrDefault<bool>("redSVD", false);
}


template<class FieldType>
void ITHACADMD<FieldType>::getModes(int SVD_rank, bool exact,
                                    bool exportDMDmodes)
{
    // Check the rank if rank < 0, full rank.
    if (SVD_rank < 0)
    {
        SVD_rank = NSnaps - 1;
    }

    std::string assertMessage = "The SVD_rank is equal to: " + name(
                                    SVD_rank) +
                                ", it cannot be bigger than the number of snapshots - 1. NSnapshots is equal to: "
                                + name(NSnaps);
    SVD_rank_public = SVD_rank;
    M_Assert(SVD_rank < NSnaps, assertMessage.c_str());
    // Convert the OpenFoam Snapshots to Matrix
    Eigen::MatrixXd SnapEigen = Foam2Eigen::PtrList2Eigen(snapshotsDMD);
    List<Eigen::MatrixXd> SnapEigenBC = Foam2Eigen::PtrList2EigenBC(snapshotsDMD);
    Eigen::MatrixXd Xm = SnapEigen.leftCols(NSnaps - 1);
    Eigen::MatrixXd Ym = SnapEigen.rightCols(NSnaps - 1);
    List<Eigen::MatrixXd> XmBC(SnapEigenBC.size());
    List<Eigen::MatrixXd> YmBC(SnapEigenBC.size());

    for (int i = 0 ; i < SnapEigenBC.size() ; i++)
    {
        XmBC[i] = SnapEigenBC[i].leftCols(NSnaps - 1);
        YmBC[i] = SnapEigenBC[i].rightCols(NSnaps - 1);
    }

    Eigen::MatrixXcd U;
    Eigen::MatrixXcd V;
    Eigen::VectorXd S;

    if (redSVD)
    {
        Info << "SVD using Randomized method" << endl;
        RedSVD::RedSVD<Eigen::MatrixXd> svd(Xm, SVD_rank);
        U = svd.matrixU();
        V = svd.matrixV();
        S = svd.singularValues().array().cwiseInverse();
    }
    else
    {
        Info << "SVD using Divide and Conquer method" << endl;
        Eigen::BDCSVD<Eigen::MatrixXd> svd(Xm,
                                           Eigen::ComputeThinU | Eigen::ComputeThinV);
        U = svd.matrixU().leftCols(SVD_rank);
        V = svd.matrixV().leftCols(SVD_rank);
        S = svd.singularValues().head(SVD_rank).array().cwiseInverse();
    }

    Eigen::MatrixXcd A_tilde = U.transpose().conjugate() * Ym *
                               V *
                               S.asDiagonal();
    Eigen::ComplexEigenSolver<Eigen::MatrixXcd> esEg(A_tilde);
    eigenValues = esEg.eigenvalues();
    Eigen::VectorXd ln = eigenValues.array().log().imag().abs();
    // Sort based on The Frequencies
    typedef std::pair<double, int> mypair;
    std::vector<mypair> sortedList(ln.size());

    for (int i = 0; i < ln.size(); i++)
    {
        sortedList[i].first = ln(i);
        sortedList[i].second = i;
    }

    std::sort(sortedList.begin(), sortedList.end(),
              std::less<std::pair<double, int>>());

    if (exact)
    {
        DMDEigenModes = Ym * V * S.asDiagonal() * esEg.eigenvectors();
        DMDEigenModesBC.resize(YmBC.size());

        for (int i = 0; i < YmBC.size(); i++)
        {
            DMDEigenModesBC[i] = YmBC[i] * V * S.asDiagonal() * esEg.eigenvectors();
        }
    }
    else
    {
        Eigen::VectorXd eigenValueseigLam =
            S.array().sqrt();
        PODm = (Xm * V) * eigenValueseigLam.asDiagonal();
        PODmBC.resize(XmBC.size());

        for (int i = 0; i < XmBC.size(); i++)
        {
            PODmBC[i] = (XmBC[i] * V) * eigenValueseigLam.asDiagonal();
        }

        DMDEigenModes = PODm * esEg.eigenvectors();
        DMDEigenModesBC.resize(PODmBC.size());

        for (int i = 0; i < PODmBC.size(); i++)
        {
            DMDEigenModesBC[i] = PODmBC[i] * esEg.eigenvectors();
        }
    }

    Amplitudes = DMDEigenModes.real().bdcSvd(Eigen::ComputeThinU |
                 Eigen::ComputeThinV).solve(Xm.col(0));

    if (exportDMDmodes)
    {
        convert2Foam();
        Info << "exporting the DMDmodes for " << snapshotsDMD[0].name() << endl;
        ITHACAstream::exportFields(DMDmodesReal.toPtrList(), "ITHACAoutput/DMD/",
                                   snapshotsDMD[0].name() + "_Modes_" + name(SVD_rank) + "_Real");
        ITHACAstream::exportFields(DMDmodesImag.toPtrList(), "ITHACAoutput/DMD/",
                                   snapshotsDMD[0].name() + "_Modes_" + name(SVD_rank) + "_Imag");
    }
}

template<class FieldType>
void ITHACADMD<FieldType>::getDynamics(double tStart, double tFinal, double dt)
{
    Eigen::VectorXcd omega = eigenValues.array().log() / originalDT;
    int ncols = static_cast<int>((tFinal - tStart) / dt ) + 1;
    dynamics.resize(SVD_rank_public, ncols);
    int i = 0;

    for (double t = tStart; t <= tFinal; t += dt)
    {
        Eigen::VectorXcd coli = (omega * t).array().exp() * Amplitudes.array();
        dynamics.col(i) = coli;
        i++;
    }
}


template<class FieldType>
void ITHACADMD<FieldType>::exportEigs(word exportFolder)
{
    Eigen::MatrixXcd eigs = eigenValues;
    mkDir(exportFolder);
    std::string path = exportFolder + "/eigs.npy";
    cnpy::save(eigs, path);
    return;
}


template<class FieldType>
void ITHACADMD<FieldType>::reconstruct(word exportFolder, word fieldName)
{
    PtrList<GeometricField<FieldType, fvPatchField, volMesh>> snapshotsrec;

    for (int i = 0; i < dynamics.cols(); i++)
    {
        Eigen::MatrixXcd col = DMDEigenModes * dynamics.col(i);
        Eigen::VectorXd vec = col.real();
        GeometricField<FieldType, fvPatchField, volMesh> tmp("TMP",
                snapshotsDMD[0] * 0);
        tmp = Foam2Eigen::Eigen2field(tmp, vec);

        for (int k = 0; k < tmp.boundaryField().size(); k++)
        {
            Eigen::VectorXd vecBC = (DMDEigenModesBC[k] * dynamics.col(i)).real();
            ITHACAutilities::assignBC(tmp, k, vecBC);
        }

        snapshotsrec.append(tmp);
    }

    ITHACAstream::exportFields(snapshotsrec, exportFolder, fieldName);
}
template<class FieldType>
void ITHACADMD<FieldType>::convert2Foam()
{
    DMDmodesReal.resize(DMDEigenModes.cols());
    DMDmodesImag.resize(DMDEigenModes.cols());
    GeometricField<FieldType, fvPatchField, volMesh> tmpReal(
        snapshotsDMD[0].name(), snapshotsDMD[0] * 0);
    GeometricField<FieldType, fvPatchField, volMesh> tmpImag(
        snapshotsDMD[0].name(), snapshotsDMD[0] * 0);

    for (label i = 0; i < DMDmodesReal.size(); i++)
    {
        Eigen::VectorXd vecReal = DMDEigenModes.col(i).real();
        Eigen::VectorXd vecImag = DMDEigenModes.col(i).imag();
        tmpReal = Foam2Eigen::Eigen2field(tmpReal, vecReal);
        tmpImag = Foam2Eigen::Eigen2field(tmpImag, vecImag);

        // Adjusting boundary conditions
        for (int k = 0; k < tmpReal.boundaryField().size(); k++)
        {
            ITHACAutilities::assignBC(tmpReal, k, DMDEigenModesBC[k].col(i).real());
            ITHACAutilities::assignBC(tmpImag, k, DMDEigenModesBC[k].col(i).imag());
        }

        DMDmodesReal.set(i, tmpReal);
        DMDmodesImag.set(i, tmpImag);
    }
}
template class ITHACADMD<scalar>;
template class ITHACADMD<vector>;
