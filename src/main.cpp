#include <iostream>
#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>
#include <string>
#include <fstream>

#include "types.hpp"
#include "vec_cal.hpp"

void setParams();
void initAtoms();
void rescaleVels();
void accumProps(int);
void singleStep(std::string);
void leapfrogStep(int);
void buildNebrList();
void computeForces();
void evalProps();
void printSummary(std::string);
void posDump(std::string);
void evalRdf(std::string);
void printRdf(std::string);
void evalLatticeCorr();
void initDiffusion();
void zeroDiffusion();
void evalDiffusion(std::string);
void accumDiffusion(std::string);
void printDiffusion(std::string);
void initVacf();
void zeroVacf();
void evalVacf(std::string);
void accumVacf(std::string);
real integrate(real *, int);
void printVacf(std::string);

// global variables
real rCut, density, temperature, deltaT, timeNow;
real uSum, virSum;
vecR cells, initUcell, region, velSum;
int nMol, nDim, stepCount, stepEquil, stepAdjTemp, stepLimit, stepAvg, stepDump;
Prop kinEnergy, totEnergy, pressure;
Mol *mol;
int *cellList;
real dispHi, rNebrShell;
int *nebrTab, nebrNow, nebrTabFac, nebrTabLen, nebrTabMax;
int num_atoms, cell_list = 0, neigh_list = 0;
real *histRdf, rangeRdf;
int countRdf, limitRdf, sizeHistRdf, stepRdf;
real latticeCorr;
Tbuff *buffer;
real *rrDiffAvg;
int countDiffAvg, limitDiffAvg, nBuffDiff, nValDiff, stepDiff;
Vbuff *vacBuff;
real *avgAcfVel, intAcfVel;
int countAcfAvg, limitAcfAvg, nBuffAcf, nValAcf, stepAcf;

int main(int argc, char **argv) {
	// program start time
	auto start = std::chrono::system_clock::now();

	// process i/o files
	std::string dot_in(argv[1]);

	nDim = 3;
	rCut = 3;
	stepLimit = 10000;
	stepEquil = 5000;
	stepAdjTemp = 20;
	stepAvg = 50;
	stepDump = 100;
	deltaT = 0.001;

	limitRdf = 100;
	rangeRdf = 4;
	sizeHistRdf = 200;
	stepRdf = 50;

	limitDiffAvg = 80;
	nBuffDiff = 20;
	nValDiff = 100;
	stepDiff = 10;

	limitAcfAvg = 80;
	nBuffAcf = 20;
	nValAcf = 100;
	stepAcf = 10;

	// input from user
	std::ifstream inputFile(dot_in);
	inputFile >> temperature >> density >> num_atoms >> cell_list >> neigh_list;
	real num_unit_cell = int(std::pow(num_atoms/4, 1/3.0)+0.5);
	initUcell = {num_unit_cell, num_unit_cell, num_unit_cell};

	// for neighbor list
	nebrTabFac = 100;
	rNebrShell = 0.4;
	nebrNow = 1;

	setParams();
	mol = new Mol[nMol];
	cellList = new int[int(vecProd(cells)+0.5) + nMol];
	nebrTab = new int[2*nebrTabMax];
	histRdf = new real[sizeHistRdf];
	rrDiffAvg = new real[nValDiff];
	buffer = new Tbuff[nBuffDiff];
	for (int nb = 0; nb < nBuffDiff; nb++) {
		buffer[nb].orgR = new vecR[nMol];
		buffer[nb].rTrue = new vecR[nMol];
		buffer[nb].rrDiff = new real[nValDiff];
	}
	avgAcfVel = new real[nValAcf];
	vacBuff = new Vbuff[nBuffAcf];
	for (int nb = 0; nb < nBuffAcf; nb++) {
		vacBuff[nb].acfVel = new real[nValAcf];
		vacBuff[nb].orgVel = new vecR[nMol];
	}

	countRdf = 0;
	initAtoms();
	accumProps(0);
	initDiffusion();
	initVacf();

	for (stepCount = 0; stepCount < stepLimit; stepCount++) {
		singleStep(dot_in);
	}

	delete[] mol;
	delete[] cellList;
	delete[] nebrTab;
	delete[] histRdf;
	delete[] rrDiffAvg;
	for (int nb = 0; nb < nBuffDiff; nb++) {
		delete[] buffer[nb].orgR;
		delete[] buffer[nb].rTrue;
		delete[] buffer[nb].rrDiff;
	}
	delete[] buffer;
	delete[] avgAcfVel;
	for (int nb = 0; nb < nBuffAcf; nb++) {
		delete[] vacBuff[nb].acfVel;
		delete[] vacBuff[nb].orgVel;
	}
	delete[] vacBuff;

	// program end time
	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end-start);
	std::string dot_out = dot_in.erase(dot_in.length()-2).append("out");
	std::ofstream outputFile;
	outputFile.open(dot_out, std::ofstream::app);
	outputFile << "Wall time: " << elapsed.count() << " seconds\n";
	outputFile.close();

	return 0;
}

void setParams() {
	vecScaleCopy(region, 1.0/std::pow(density/4.0, 1/3.0), initUcell);
	vecScaleCopy(cells, 1.0/rCut, region);
	vecRound(cells);
	nMol = 4 * int(vecProd(initUcell)+0.5);
	nebrTabMax = nebrTabFac * nMol;
}

void initAtoms() {
	vecR c, gap;
	int n = 0;
	vecDiv(gap, region, initUcell);
	for (int nz = 0; nz < initUcell.z; nz++) {
		for (int ny = 0; ny < initUcell.y; ny++) {
			for (int nx = 0; nx < initUcell.x; nx++) {
				vecSet(c, nx+0.25, ny+0.25, nz+0.25);
				vecMul(c, c, gap);
				vecScaleAdd(c, c, -0.5, region);
				for (int j = 0; j < 4; j++) {
					mol[n].r = c;
					switch (j) {
						case 0:
							mol[n].r.x += 0.5 * gap.x;
							mol[n].r.y += 0.5 * gap.y;
							break;
						case 1:
							mol[n].r.y += 0.5 * gap.y;
							mol[n].r.z += 0.5 * gap.z;
							break;
						case 2:
							mol[n].r.z += 0.5 * gap.z;
							mol[n].r.x += 0.5 * gap.x;
							break;
					}
					n++;
				}
			}
		}
	}

	// radom velocity generator
	std::default_random_engine rand_gen;
	std::normal_distribution<real> normal_dist(0.0, 1.0);

	vecSet(velSum, 0, 0, 0);
	for (int i = 0; i < nMol; i++) {
		vecSet(mol[i].vel, normal_dist(rand_gen),
				normal_dist(rand_gen), normal_dist(rand_gen));
		vecAdd(velSum, velSum, mol[i].vel);
		// assign zero init. acceleration
		vecSet(mol[i].acc, 0, 0, 0);
	}

	// account for COM shift
	for (int i = 0; i < nMol; i++) {
		vecScaleAdd(mol[i].vel, mol[i].vel, -1.0/nMol, velSum);
	}

	// adjust temperature
	rescaleVels();
}

void rescaleVels() {
	real velSqSum = 0;
	for (int i = 0; i < nMol; i++) {
		velSqSum += vecLenSq(mol[i].vel);
	}

	real lambda = std::sqrt(3 * (nMol - 1) * temperature / velSqSum);
	for (int i = 0; i < nMol; i++) {
		vecScale(mol[i].vel, lambda);
	}
}

void accumProps(int icode) {
	switch (icode) {
		case 0:
			propZero(kinEnergy);
			propZero(totEnergy);
			propZero(pressure);
			break;
		case 1:
			propAccum(kinEnergy);
			propAccum(totEnergy);
			propAccum(pressure);
			break;
		case 2:
			propAvg(kinEnergy, stepAvg);
			propAvg(totEnergy, stepAvg);
			propAvg(pressure, stepAvg);
			break;
	}
}

void singleStep(std::string dot_in) {
	timeNow = stepCount * deltaT;

	leapfrogStep(1);
	// apply boundary conditions
	for (int i = 0; i < nMol; i++) {
		vecWrapAll(mol[i].r, region);
	}

	// execute this when neigh_list is on
	// and nebrNow is 1
	if (neigh_list && nebrNow) {
		nebrNow = 0;
		dispHi = 0;
		buildNebrList();
	}

	computeForces();
	leapfrogStep(2);
	evalProps();
	accumProps(1);

	// rescale velocities
	if ((stepCount < stepEquil) && !(stepCount % stepAdjTemp)) {
		rescaleVels();
	}

	if (stepCount % stepAvg == 0) {
		accumProps(2);
		evalLatticeCorr();
		printSummary(dot_in);
		accumProps(0);
	}

	if (stepCount % stepDump == 0) {
		posDump(dot_in);
	}

	if (stepCount >= stepEquil && (stepCount - stepEquil) % stepRdf == 0) {
		evalRdf(dot_in);
	}

	if (stepCount >= stepEquil && (stepCount - stepEquil) % stepDiff == 0) {
		evalDiffusion(dot_in);
	}

	if (stepCount >= stepEquil && (stepCount - stepEquil) % stepAcf == 0) {
		evalVacf(dot_in);
	}
}

void leapfrogStep(int part) {
	if (part == 1) {
		for (int i = 0; i < nMol; i++) {
			vecScaleAdd(mol[i].vel, mol[i].vel, 0.5*deltaT, mol[i].acc);
			vecScaleAdd(mol[i].r, mol[i].r, deltaT, mol[i].vel);
		}
	} else {
		for (int i = 0; i < nMol; i++) {
			vecScaleAdd(mol[i].vel, mol[i].vel, 0.5*deltaT, mol[i].acc);
		}
	}
}

void buildNebrList() {
	vecR dr, invWid, rs, shift, cc, m1v, m2v;
	vecR vecOffset[] = {{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {-1,1,0}, {0,0,1}, {1,0,1},
			{1,1,1}, {0,1,1}, {-1,1,1}, {-1,0,1}, {-1,-1,1}, {0,-1,1}, {1,-1,1}};
	real rrNebr, rr;

	rrNebr = Sqr(rCut + rNebrShell);
	nebrTabLen = 0;

	if (cell_list) {
		/*
		 * CELL SUBDIVISION FOR NEIGHBOR LIST
		 */
		vecDiv(invWid, cells, region);
		// initialize the cell values to -1
		for (int i = nMol; i < nMol + vecProd(cells); i++) {
			cellList[i] = -1;
		}
	
		// make a linked list
		for (int i = 0; i < nMol; i++) {
			vecScaleAdd(rs, mol[i].r, 0.5, region);
			vecMul(cc, rs, invWid);
			vecFloor(cc);
			int c = vecLinear(cc, cells) + nMol;
			cellList[i] = cellList[c];
			cellList[c] = i;
		}
	
		for (int m1z = 0; m1z < cells.z; m1z++) {
			for (int m1y = 0; m1y < cells.y; m1y++) {
				for (int m1x = 0; m1x < cells.x; m1x++) {
					vecSet(m1v, m1x, m1y, m1z);
					int m1 = vecLinear(m1v, cells) + nMol;
					for (int Noff = 0; Noff < 14; Noff++) {
						vecAdd(m2v, m1v, vecOffset[Noff]);
						vecSet(shift, 0, 0, 0);
						cellWrapAll(m2v, shift, cells, region);
						int m2 = vecLinear(m2v, cells) + nMol;
						for (int j1 = cellList[m1]; j1 > -1; j1 = cellList[j1]) {
							for (int j2 = cellList[m2]; j2 > -1; j2 = cellList[j2]) {
								if (m1 != m2 || j1 > j2) {
									vecSub(dr, mol[j1].r, mol[j2].r);
									vecSub(dr, dr, shift);
									rr = vecLenSq(dr);
									if (rr < rrNebr) {
										if (nebrTabLen >= nebrTabMax) {
											std::cout << "too many neighbors!\n";
											exit(0);
										}
										nebrTab[2*nebrTabLen] = j1;
										nebrTab[2*nebrTabLen+1] = j2;
										nebrTabLen++;
									}
								}
							}
						}
					}
				}
			}
		}
	} else {
		/*
		 * ONLY NEIGHBOR LIST
		 */
		for (int j1 = 0; j1 < nMol - 1; j1++) {
			for (int j2 = j1+1; j2 < nMol; j2++) {
				vecSub(dr, mol[j1].r, mol[j2].r);
				vecWrapAll(dr, region);
				rr = vecLenSq(dr);
				if (rr < rrNebr) {
					if (nebrTabLen >= nebrTabMax) {
						std::cout << "too many neighbors!\n";
						exit(0);
					}
					nebrTab[2*nebrTabLen] = j1;
					nebrTab[2*nebrTabLen+1] = j2;
					nebrTabLen++;
				}
			}
		}
	}
}

void computeForces() {
	vecR dr, invWid, rs, shift, cc, m1v, m2v;
	vecR vecOffset[] = {{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {-1,1,0}, {0,0,1}, {1,0,1},
			{1,1,1}, {0,1,1}, {-1,1,1}, {-1,0,1}, {-1,-1,1}, {0,-1,1}, {1,-1,1}};
	real fcVal, rr, rrCut, rri, rri3;

	rrCut = Sqr(rCut);
	// resetting the acc. values since they are incremented later on
	for (int i = 0; i < nMol; i++) {
		vecSet(mol[i].acc, 0, 0, 0);
	}
	uSum = 0;
	virSum = 0;

	if (neigh_list) {
		/*
		 * NEIGHBOR LIST
		 */
		for (int i = 0; i < nebrTabLen; i++) {
			int j1 = nebrTab[2*i];
			int j2 = nebrTab[2*i+1];
			vecSub(dr, mol[j1].r, mol[j2].r);
			vecWrapAll(dr, region);
			rr = vecLenSq(dr);
			if (rr < rrCut) {
				rri = 1.0 / rr;
				rri3 = Cub(rri);
				fcVal = 48.0 * rri3 * (rri3 - 0.5) * rri;
				vecScaleAdd(mol[j1].acc, mol[j1].acc, fcVal, dr);
				vecScaleAdd(mol[j2].acc, mol[j2].acc, -fcVal, dr);
				uSum += 4.0 * rri3 * (rri3 - 1.0);
				virSum += fcVal * rr;
			}
		}
	} else if (cell_list) {
		/*
		 * CELL SUBDIVISION
		 */
		vecDiv(invWid, cells, region);
		// initialize the cell values to -1
		for (int i = nMol; i < nMol + vecProd(cells); i++) {
			cellList[i] = -1;
		}
	
		// make a linked list
		for (int i = 0; i < nMol; i++) {
			vecScaleAdd(rs, mol[i].r, 0.5, region);
			vecMul(cc, rs, invWid);
			vecFloor(cc);
			int c = vecLinear(cc, cells) + nMol;
			cellList[i] = cellList[c];
			cellList[c] = i;
		}
	
		for (int m1z = 0; m1z < cells.z; m1z++) {
			for (int m1y = 0; m1y < cells.y; m1y++) {
				for (int m1x = 0; m1x < cells.x; m1x++) {
					vecSet(m1v, m1x, m1y, m1z);
					int m1 = vecLinear(m1v, cells) + nMol;
					for (int Noff = 0; Noff < 14; Noff++) {
						vecAdd(m2v, m1v, vecOffset[Noff]);
						vecSet(shift, 0, 0, 0);
						cellWrapAll(m2v, shift, cells, region);
						int m2 = vecLinear(m2v, cells) + nMol;
						for (int j1 = cellList[m1]; j1 > -1; j1 = cellList[j1]) {
							for (int j2 = cellList[m2]; j2 > -1; j2 = cellList[j2]) {
								if (m1 != m2 || j1 > j2) {
									vecSub(dr, mol[j1].r, mol[j2].r);
									vecSub(dr, dr, shift);
									rr = vecLenSq(dr);
									if (rr < rrCut) {
										rri = 1.0 / rr;
										rri3 = Cub(rri);
										fcVal = 48.0 * rri3 * (rri3 - 0.5) * rri;
										vecScaleAdd(mol[j1].acc, mol[j1].acc, fcVal, dr);
										vecScaleAdd(mol[j2].acc, mol[j2].acc, -fcVal, dr);
										uSum += 4.0 * rri3 * (rri3 - 1.0);
										virSum += fcVal * rr;
									}
								}
							}
						}
					}
				}
			}
		}
	} else {
		/*
		 * ALL PAIRS
		 */
		for (int j1 = 0; j1 < nMol - 1; j1++) {
			for (int j2 = j1+1; j2 < nMol; j2++) {
				vecSub(dr, mol[j1].r, mol[j2].r);
				vecWrapAll(dr, region);
				rr = vecLenSq(dr);
				if (rr < rrCut) {
					rri = 1.0 / rr;
					rri3 = Cub(rri);
					fcVal = 48.0 * rri3 * (rri3 - 0.5) * rri;
					vecScaleAdd(mol[j1].acc, mol[j1].acc, fcVal, dr);
					vecScaleAdd(mol[j2].acc, mol[j2].acc, -fcVal, dr);
					uSum += 4.0 * rri3 * (rri3 - 1.0);
					virSum += fcVal * rr;
				}
			}
		}
	}
}

void evalProps() {
	vecSet(velSum, 0, 0, 0);
	real v2, v2sum = 0, v2max = 0;

	for (int i = 0; i < nMol; i++) {
		vecAdd(velSum, velSum, mol[i].vel);
		v2 = vecLenSq(mol[i].vel);
		v2sum += v2;
		v2max = std::max(v2max, v2);
	}

	kinEnergy.val = 0.5 * v2sum / nMol;
	totEnergy.val = kinEnergy.val + uSum / nMol;
	pressure.val = density * (v2sum + virSum) / (nMol * nDim);

	dispHi += std::sqrt(v2max) * deltaT;
	if (dispHi > 0.5 * rNebrShell) {
		nebrNow = 1;
	}
}

void printSummary(std::string dot_in) {
	std::string dot_out = dot_in.erase(dot_in.length()-2).append("out");
	std::ofstream outputFile;
	outputFile.open(dot_out, std::ofstream::app);
	outputFile << stepCount << '\t' << timeNow << '\t'
		<< std::sqrt(vecLenSq(velSum))/nMol << '\t'
		<< kinEnergy.sum << '\t' << totEnergy.sum << '\t'
		<< pressure.sum << '\t' << latticeCorr << '\n';
	outputFile.close();
}

void posDump(std::string dot_in) {
	std::string dot_dump = dot_in.erase(dot_in.length()-2).append("dump");
	std::ofstream dumpFile;
	dumpFile.open(dot_dump, std::ofstream::app);
	dumpFile << "ITEM: TIMESTEP\n" << timeNow << '\n'
		<< "ITEM: NUMBER OF ATOMS\n" << nMol << '\n'
		<< "ITEM: BOX BOUNDS pp pp pp\n"
		<< -0.5*region.x << ' ' << 0.5*region.x << '\n'
		<< -0.5*region.y << ' ' << 0.5*region.y << '\n'
		<< -0.5*region.z << ' ' << 0.5*region.z << '\n'
		<< "ITEM: ATOMS id x y z\n";
	for (int i = 0; i < nMol; i++) {
		dumpFile << i+1 << ' '
			<< mol[i].r.x << ' ' << mol[i].r.y << ' ' << mol[i].r.z << '\n';
	}
	dumpFile.close();
}

void evalRdf(std::string dot_in) {
	vecR dr;
	real deltaR, normFac, rr;

	if (countRdf == 0) {
		for (int n = 0; n < sizeHistRdf; n++) {
			histRdf[n] = 0;
		}
	}
	deltaR = rangeRdf / sizeHistRdf;

	for (int j1 = 0; j1 < nMol - 1; j1++) {
		for (int j2 = j1 + 1; j2 < nMol; j2++) {
			vecSub(dr, mol[j1].r, mol[j2].r);
			vecWrapAll(dr, region);
			rr = vecLenSq(dr);
			if (rr < Sqr(rangeRdf)) {
				int n = std::sqrt(rr) / deltaR;
				histRdf[n]++;
			}
		}
	}

	countRdf++;
	if (countRdf == limitRdf) {
		normFac = vecProd(region)
			/ (2.0 * 3.141592654 * Cub(deltaR) * Sqr(nMol) * countRdf);
		for (int n = 0; n < sizeHistRdf; n++) {
			histRdf[n] *= normFac / Sqr(n - 0.5);
		}
		printRdf(dot_in);
		countRdf = 0;
	}
}

void printRdf(std::string dot_in) {
	std::string dot_rdf = dot_in.erase(dot_in.length()-2).append("rdf");
	std::ofstream rdfFile;
	rdfFile.open(dot_rdf, std::ofstream::app);

	rdfFile << "RDF\n";
	for (int n = 0; n < sizeHistRdf; n++) {
		real rb = (n + 0.5) * rangeRdf / sizeHistRdf;
		rdfFile << rb << '\t' << histRdf[n] << '\n';
	}

	rdfFile.close();
}

void evalLatticeCorr() {
	vecR kVec;
	real si = 0, sr = 0, t;

	kVec.x = 2.0 * 3.141592654 * initUcell.x / region.x;
	kVec.y = - kVec.x;
	kVec.z = kVec.x;

	for (int n = 0; n < nMol; n++) {
		t = vecDot(kVec, mol[n].r);
		sr += std::cos(t);
		si += std::sin(t);
	}

	latticeCorr = std::sqrt(Sqr(sr) + Sqr(si)) / nMol;
}

void initDiffusion() {
	for (int nb = 0; nb < nBuffDiff; nb++) {
		buffer[nb].count = -nb * nValDiff / nBuffDiff;
	}
	zeroDiffusion();
}

void zeroDiffusion() {
	countDiffAvg = 0;
	for (int j = 0; j < nValDiff; j++) {
		rrDiffAvg[j] = 0;
	}
}

void evalDiffusion(std::string dot_in) {
	vecR dr;
	for (int nb = 0; nb < nBuffDiff; nb++) {
		if (buffer[nb].count == 0) {
			for (int n = 0; n < nMol; n++) {
				buffer[nb].orgR[n] = mol[n].r;
				buffer[nb].rTrue[n] = mol[n].r;
			}
		}
		if (buffer[nb].count >= 0) {
			int ni = buffer[nb].count;
			buffer[nb].rrDiff[ni] = 0;
			for (int n = 0; n < nMol; n++) {
				vecSub(dr, buffer[nb].rTrue[n], mol[n].r);
				vecDiv(dr, dr, region);
				vecRound(dr);
				vecMul(dr, dr, region);
				vecAdd(buffer[nb].rTrue[n], mol[n].r, dr);
				vecSub(dr, buffer[nb].rTrue[n], buffer[nb].orgR[n]);
				buffer[nb].rrDiff[ni] += vecLenSq(dr);
			}
		}
		buffer[nb].count++;
	}

	accumDiffusion(dot_in);
}

void accumDiffusion(std::string dot_in) {
	real fac;
	for (int nb = 0; nb < nBuffDiff; nb++) {
		if (buffer[nb].count == nValDiff) {
			for (int j = 0; j < nValDiff; j++) {
				rrDiffAvg[j] += buffer[nb].rrDiff[j];
			}
			buffer[nb].count = 0;
			countDiffAvg++;
			if (countDiffAvg == limitDiffAvg) {
				fac = 1.0 / (nDim * 2 * nMol * stepDiff * deltaT * limitDiffAvg);
				for (int k = 1; k < nValDiff; k++) {
					rrDiffAvg[k] *= fac / k;
				}
				printDiffusion(dot_in);
				zeroDiffusion();
			}
		}
	}
}

void printDiffusion(std::string dot_in) {
	std::string dot_dfs = dot_in.erase(dot_in.length()-2).append("dfs");
	std::ofstream dfsFile;
	dfsFile.open(dot_dfs, std::ofstream::app);

	real tVal;
	dfsFile << "Diffusion\n";
	for (int j = 0; j < nValDiff; j++) {
		tVal = j * stepDiff * deltaT;
		dfsFile << tVal << '\t' << rrDiffAvg[j] << '\n';
	}

	dfsFile.close();
}

void initVacf() {
	for (int nb = 0; nb < nBuffAcf; nb++) {
		vacBuff[nb].count = -nb * nValAcf / nBuffAcf;
	}
	zeroVacf();
}

void zeroVacf() {
	countAcfAvg = 0;
	for (int j = 0; j < nValAcf; j++) {
		avgAcfVel[j] = 0;
	}
}

void evalVacf(std::string dot_in) {
	for (int nb = 0; nb < nBuffAcf; nb++) {
		if (vacBuff[nb].count == 0) {
			for (int n = 0; n < nMol; n++) {
				vacBuff[nb].orgVel[n] = mol[n].vel;
			}
		}
		if (vacBuff[nb].count >= 0) {
			int ni = vacBuff[nb].count;
			vacBuff[nb].acfVel[ni] = 0;
			for (int n = 0; n < nMol; n++) {
				vacBuff[nb].acfVel[ni] += vecDot(vacBuff[nb].orgVel[n], mol[n].vel);
			}
		}
		vacBuff[nb].count++;
	}

	accumVacf(dot_in);
}

void accumVacf(std::string dot_in) {
	real fac;
	for (int nb = 0; nb < nBuffAcf; nb++) {
		if (vacBuff[nb].count == nValAcf) {
			for (int j = 0; j < nValAcf; j++) {
				avgAcfVel[j] += vacBuff[nb].acfVel[j];
			}
			vacBuff[nb].count = 0;
			countAcfAvg++;
			if (countAcfAvg == limitAcfAvg) {
				fac = stepAcf * deltaT / (nDim * nMol * limitAcfAvg);
				intAcfVel = fac * integrate(avgAcfVel, nValAcf);
				for (int k = 1; k < nValAcf; k++) {
					avgAcfVel[k] /= avgAcfVel[0];
				}
				avgAcfVel[0] = 1;
				printVacf(dot_in);
				zeroVacf();
			}
		}
	}
}

real integrate(real *f, int nf) {
	real s = 0.5 * (f[0] + f[nf-1]);
	for (int i = 1; i < nf-1; i++) {
		s += f[i];
	}
	return s;
}

void printVacf(std::string dot_in) {
	std::string dot_acf = dot_in.erase(dot_in.length()-2).append("acf");
	std::ofstream acfFile;
	acfFile.open(dot_acf, std::ofstream::app);

	real tVal;
	acfFile << "VACF\n";
	for (int j = 0; j < nValAcf; j++) {
		tVal = j * stepAcf * deltaT;
		acfFile << tVal << '\t' << avgAcfVel[j] << '\n';
	}
	acfFile << "VACF integral: " << intAcfVel << '\n';

	acfFile.close();
}
