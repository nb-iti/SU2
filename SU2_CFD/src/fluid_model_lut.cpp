/*!
 * fluid_model_lut.cpp
 * \brief Source of the look-up table model.
 * \author S. Vitale, A. Rubino
 * \version 4.1.2 "Cardinal"
 *
 * SU2 Lead Developers: Dr. Francisco Palacios (Francisco.D.Palacios@boeing.com).
 *                      Dr. Thomas D. Economon (economon@stanford.edu).
 *
 * SU2 Developers: Prof. Juan J. Alonso's group at Stanford University.
 *                 Prof. Piero Colonna's group at Delft University of Technology.
 *                 Prof. Nicolas R. Gauger's group at Kaiserslautern University of Technology.
 *                 Prof. Alberto Guardone's group at Polytechnic University of Milan.
 *                 Prof. Rafael Palacios' group at Imperial College London.
 *
 * Copyright (C) 2012-2016 SU2, the open-source CFD code.
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#include "../include/fluid_model_lut.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <string>
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>
#include "stdlib.h"
#include "stdio.h"
#include <iomanip>
#include <vector>

using namespace std;

CTrapezoidalMap::CTrapezoidalMap() {
}
CTrapezoidalMap::CTrapezoidalMap(vector<su2double> const &x_samples,
		vector<su2double> const &y_samples,
		vector<vector<int> > const &unique_edges) {
	rank = MASTER_NODE;

#ifdef HAVE_MPI
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

	clock_t build_start = clock();

	Unique_X_Bands = x_samples; //copy the x_values in
	//Sort the x_bands and make them unique
	sort(Unique_X_Bands.begin(), Unique_X_Bands.end());
	vector<su2double>::iterator iter;
	iter = unique(Unique_X_Bands.begin(), Unique_X_Bands.end());
	Unique_X_Bands.resize(distance(Unique_X_Bands.begin(), iter));
	X_Limits_of_Edges.resize(unique_edges.size(), vector<su2double>(2, 0));
	Y_Limits_of_Edges.resize(unique_edges.size(), vector<su2double>(2, 0));

	//Store the x and y values of each edge into a vector for a slight speed up as it
	//prevents some uncoalesced accesses
	for (unsigned int j = 0; j < unique_edges.size(); j++) {
		X_Limits_of_Edges[j][0] = x_samples[unique_edges[j][0]];
		X_Limits_of_Edges[j][1] = x_samples[unique_edges[j][1]];
		Y_Limits_of_Edges[j][0] = y_samples[unique_edges[j][0]];
		Y_Limits_of_Edges[j][1] = y_samples[unique_edges[j][1]];
	}

	//How many bands to search?
	int b_max = Unique_X_Bands.size() - 1;
	//Start with band 0, obviously
	int b = 0;
	//How many edges to check for intersection with the band?
	int e_max = unique_edges.size();
	//Start with edge indexes as 0.
	int i = 0;
	//Count the how many edges intersect a band
	int k = 0;
	//The high and low x value of each band
	su2double x_low = 0;
	su2double x_hi = 0;

	//Store the y_values of edges as required for searching
	Y_Values_of_Edge_Within_Band_And_Index.resize(Unique_X_Bands.size());

	//Check which edges intersect the band
	while (b < (b_max)) {
		x_low = Unique_X_Bands[b];
		x_hi = Unique_X_Bands[b + 1];
		i = 0;
		k = 0;
		//This while loop determined which edges appear in a paritcular band
		//The index of the edge being tested is 'i'
		while (i < e_max) {
			//Check if edge intersects the band (vertical edges are automatically discared)
			if (((X_Limits_of_Edges[i][0] <= x_low)
					and (X_Limits_of_Edges[i][1] >= x_hi))
					or ((X_Limits_of_Edges[i][1] <= x_low)
							and (X_Limits_of_Edges[i][0] >= x_hi))) {
				Y_Values_of_Edge_Within_Band_And_Index[b].push_back(make_pair(0.0, 0));
				//Save the edge index so it can latter be recalled (when searching)
				Y_Values_of_Edge_Within_Band_And_Index[b][k].second = i;
				//Determine what y value the edge takes in the middle of the band
				Y_Values_of_Edge_Within_Band_And_Index[b][k].first =
						Y_Limits_of_Edges[i][0]
								+ (Y_Limits_of_Edges[i][1] - Y_Limits_of_Edges[i][0])
										/ (X_Limits_of_Edges[i][1] - X_Limits_of_Edges[i][0])
										* ((x_low + x_hi) / 2.0 - X_Limits_of_Edges[i][0]);
				//k counts the number of edges which have been found to intersect with the band
				k++;
			}
			//increment i, which  moves the algorithm along to the next edge
			i++;
		}
		//Sort the edges in the band depending on the y values they were found to have
		//It is worth noting that these y values are unique (i.e. edges cannot intersect in a band)
		sort(Y_Values_of_Edge_Within_Band_And_Index[b].begin(),
				Y_Values_of_Edge_Within_Band_And_Index[b].end());
		//Move on to the next band of x values
		b++;
	}

	su2double duration = ((su2double) clock() - (su2double) build_start)
			/ ((su2double) CLOCKS_PER_SEC);
	if (rank == MASTER_NODE)
		if (rank == MASTER_NODE) cout << duration << " seconds\n";
}

void CTrapezoidalMap::Find_Containing_Simplex(su2double x, su2double y) {
	//Find the x band in which the current x value sits
	Search_Bands_For(x);
	//Within that band find the two containing edges
	Search_Band_For_Edge(x, y);
}

void CTrapezoidalMap::Search_Bands_For(su2double x) {
	su2double x00;
	UpperI = Unique_X_Bands.size() - 1;
	LowerI = 0;

	while (UpperI - LowerI > 1) {
		middleI = (UpperI + LowerI) / 2;
		x00 = Unique_X_Bands[middleI];
		if (x00 > x) {
			UpperI = middleI;
		} else if (x00 < x) {
			LowerI = middleI;
		} else if (x00 == x) {
			LowerI = middleI;
			UpperI = LowerI + 1;
			break;
		}

	}
}

void CTrapezoidalMap::Search_Band_For_Edge(su2double x, su2double y) {

	su2double RunVal, y00, y10, x00, x10;
	int RunEdge;
	UpperJ = Y_Values_of_Edge_Within_Band_And_Index[LowerI].size() - 1;
	LowerJ = 0;

	while (UpperJ - LowerJ > 1) {
		middleJ = (UpperJ + LowerJ) / 2;
		//Select the edge associated with the current x band (LowerI)
		//Search for the RunEdge in the middleJ direction (second value is index of edge)
		RunEdge = Y_Values_of_Edge_Within_Band_And_Index[LowerI][middleJ].second;
		y00 = Y_Limits_of_Edges[RunEdge][0];
		y10 = Y_Limits_of_Edges[RunEdge][1];
		x00 = X_Limits_of_Edges[RunEdge][0];
		x10 = X_Limits_of_Edges[RunEdge][1];
		//The search variable in j should be interpolated in i as well
		RunVal = y00 + (y10 - y00) / (x10 - x00) * (x - x00);
		if (RunVal > y) {
			UpperJ = middleJ;
		} else if (RunVal < y) {
			LowerJ = middleJ;
		} else if (RunVal == y) {
			LowerJ = middleJ;
			UpperJ = LowerJ + 1;
			break;
		}

	}
	UpperEdge = Y_Values_of_Edge_Within_Band_And_Index[LowerI][UpperJ].second;
	LowerEdge = Y_Values_of_Edge_Within_Band_And_Index[LowerI][LowerJ].second;
}

CLookUpTable::CLookUpTable(CConfig *config, bool dimensional) :
		CFluidModel() {
	LUT_Debug_Mode = false;
	rank = MASTER_NODE;
	CurrentPoints.resize(4, 0);
	LUT_Debug_Mode = config->GetLUT_Debug_Mode();

#ifdef HAVE_MPI
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

	if (dimensional) {
		Pressure_Reference_Value = 1;
		Temperature_Reference_Value = 1;
		Density_Reference_Value = 1;
		Velocity_Reference_Value = 1;
		Energy_Reference_Value = 1;
	} else {
		Pressure_Reference_Value = config->GetPressure_Ref();
		Temperature_Reference_Value = config->GetTemperature_Ref();
		Density_Reference_Value = config->GetDensity_Ref();
		Velocity_Reference_Value = config->GetVelocity_Ref();
		Energy_Reference_Value = config->GetEnergy_Ref();
	}

	if ((config->GetLUTFileName()).find(".tec") != string::npos) {
		if (rank == MASTER_NODE) {
			cout << ".tec type LUT found" << endl;
		}
		LookUpTable_Load_TEC(config->GetLUTFileName());
	} else {
		if (rank == MASTER_NODE) {
			cout << "No recognized LUT format found, exiting!" << endl;
		}
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			Interpolation_Coeff[i][j] = -1.0;
		}
	}
	if (rank == MASTER_NODE) {
// Give the user some information on the size of the table
		cout << "Number of stations  in zone 0: " << nTable_Zone_Stations[0]
				<< endl;
		cout << "Number of triangles in zone 0: " << nTable_Zone_Triangles[0]
				<< endl;
		cout << "Number of stations  in zone 1: " << nTable_Zone_Stations[1]
				<< endl;
		cout << "Number of triangles in zone 1: " << nTable_Zone_Triangles[1]
				<< endl;
		cout << "Detecting all unique edges..." << endl;
	}
	Get_Unique_Edges();

	if (rank == MASTER_NODE) {
// Building an KD_tree for the HS thermopair
		cout << "Building trapezoidal map for rhoe..." << endl;
	}

	rhoe_map[0] = CTrapezoidalMap(ThermoTables_Density[0],
			ThermoTables_StaticEnergy[0], Table_Zone_Edges[0]);
	rhoe_map[1] = CTrapezoidalMap(ThermoTables_Density[1],
			ThermoTables_StaticEnergy[1], Table_Zone_Edges[1]);

	if (rank == MASTER_NODE) {
		cout << "Building trapezoidal map for Prho..." << endl;
	}
	Prho_map[0] = CTrapezoidalMap(ThermoTables_Pressure[0],
			ThermoTables_Density[0], Table_Zone_Edges[0]);
	Prho_map[1] = CTrapezoidalMap(ThermoTables_Pressure[1],
			ThermoTables_Density[1], Table_Zone_Edges[1]);

	if (rank == MASTER_NODE) {
		cout << "Building trapezoidal map for hs..." << endl;
	}
	hs_map[0] = CTrapezoidalMap(ThermoTables_Enthalpy[0], ThermoTables_Entropy[0],
			Table_Zone_Edges[0]);
	hs_map[1] = CTrapezoidalMap(ThermoTables_Enthalpy[1], ThermoTables_Entropy[1],
			Table_Zone_Edges[1]);

	if (rank == MASTER_NODE) {
		cout << "Building trapezoidal map for Ps..." << endl;
	}
	Ps_map[0] = CTrapezoidalMap(ThermoTables_Pressure[0], ThermoTables_Entropy[0],
			Table_Zone_Edges[0]);
	Ps_map[1] = CTrapezoidalMap(ThermoTables_Pressure[1], ThermoTables_Entropy[1],
			Table_Zone_Edges[1]);

	if (rank == MASTER_NODE) {
		cout << "Building trapezoidal map for rhoT..." << endl;
	}
	rhoT_map[0] = CTrapezoidalMap(ThermoTables_Density[0],
			ThermoTables_Temperature[0], Table_Zone_Edges[0]);
	;
	rhoT_map[1] = CTrapezoidalMap(ThermoTables_Density[1],
			ThermoTables_Temperature[1], Table_Zone_Edges[1]);
	;
	if (rank == MASTER_NODE) {
		cout << "Building trapezoidal map for PT (in vapor region only)..." << endl;
	}
	PT_map[1] = CTrapezoidalMap(ThermoTables_Pressure[1],
			ThermoTables_Temperature[1], Table_Zone_Edges[1]);
	;
	PT_map[0] = PT_map[1];

	if (rank == MASTER_NODE) {
		cout << "Print LUT errors? (LUT_Debug_Mode):  " << LUT_Debug_Mode << endl;
	}

	CurrentZone = 1;		//The vapor region

}

CLookUpTable::~CLookUpTable(void) {
// Using vectors so no need to deallocate

}

void CLookUpTable::Get_Unique_Edges() {
//Import all potential edges into a vector
	for (int j = 0; j < 2; j++) {
		Table_Zone_Edges[j].resize(3 * nTable_Zone_Triangles[j], vector<int>(2, 0));
//Fill with edges (based on triangulation
		for (int i = 0; i < nTable_Zone_Triangles[j]; i++) {
			int smaller_point, larger_point;
			smaller_point = Table_Zone_Triangles[j][i][0];
			larger_point = Table_Zone_Triangles[j][i][1];
			Table_Zone_Edges[j][3 * i + 0][0] = min(smaller_point, larger_point);
			Table_Zone_Edges[j][3 * i + 0][1] = max(smaller_point, larger_point);
			smaller_point = Table_Zone_Triangles[j][i][1];
			larger_point = Table_Zone_Triangles[j][i][2];
			Table_Zone_Edges[j][3 * i + 1][0] = min(smaller_point, larger_point);
			Table_Zone_Edges[j][3 * i + 1][1] = max(smaller_point, larger_point);
			smaller_point = Table_Zone_Triangles[j][i][2];
			larger_point = Table_Zone_Triangles[j][i][0];
			Table_Zone_Edges[j][3 * i + 2][0] = min(smaller_point, larger_point);
			Table_Zone_Edges[j][3 * i + 2][1] = max(smaller_point, larger_point);
		}
//Sort the edges to enable selecting unique entries only
		sort(Table_Zone_Edges[j].begin(), Table_Zone_Edges[j].end());
//Make the list of edges unique
		vector<vector<int> >::iterator iter;
		iter = unique(Table_Zone_Edges[j].begin(), Table_Zone_Edges[j].end());
		Table_Zone_Edges[j].resize(distance(Table_Zone_Edges[j].begin(), iter));
	}

//Filter out all the edges which have been imported twice
}

void CLookUpTable::Get_Current_Points_From_TrapezoidalMap(
		CTrapezoidalMap *t_map, su2double x, su2double y) {
	CurrentPoints.resize(4, 0);
	t_map[CurrentZone].Find_Containing_Simplex(x, y);
	CurrentPoints[0] =
			Table_Zone_Edges[CurrentZone][t_map[CurrentZone].getLowerEdge()][0];
	CurrentPoints[1] =
			Table_Zone_Edges[CurrentZone][t_map[CurrentZone].getLowerEdge()][1];
	CurrentPoints[2] =
			Table_Zone_Edges[CurrentZone][t_map[CurrentZone].getUpperEdge()][0];
	CurrentPoints[3] =
			Table_Zone_Edges[CurrentZone][t_map[CurrentZone].getUpperEdge()][1];
}

void CLookUpTable::SetTDState_rhoe(su2double rho, su2double e) {
// Check if inputs are in total range (necessary but not sufficient condition)

	Get_Current_Points_From_TrapezoidalMap(rhoe_map, rho, e);

	//Now use the quadrilateral which contains the point to interpolate
	//Determine the interpolation coefficients
	Interpolate_2D_Bilinear(rho, e, ThermoTables_Density,
			ThermoTables_StaticEnergy, "RHOE");

//Interpolate the fluid properties
	StaticEnergy = e;
	Density = rho;
	Entropy = Interpolate_2D_Bilinear(ThermoTables_Entropy);
	Pressure = Interpolate_2D_Bilinear(ThermoTables_Pressure);
	Enthalpy = Interpolate_2D_Bilinear(ThermoTables_Enthalpy);
	SoundSpeed2 = Interpolate_2D_Bilinear(ThermoTables_SoundSpeed2);
	Temperature = Interpolate_2D_Bilinear(ThermoTables_Temperature);
	dPdrho_e = Interpolate_2D_Bilinear(ThermoTables_dPdrho_e);
	dPde_rho = Interpolate_2D_Bilinear(ThermoTables_dPde_rho);
	dTdrho_e = Interpolate_2D_Bilinear(ThermoTables_dTdrho_e);
	dTde_rho = Interpolate_2D_Bilinear(ThermoTables_dTde_rho);
	Cp = Interpolate_2D_Bilinear(ThermoTables_Cp);
	//Mu = Interpolate_2D_Bilinear(ThermoTables_Mu);
	//Kt = Interpolate_2D_Bilinear(ThermoTables_Kt);

}

void CLookUpTable::SetTDState_PT(su2double P, su2double T) {
// Check if inputs are in total range (necessary but not sufficient condition)
	Get_Current_Points_From_TrapezoidalMap(PT_map, P, T);
	//Determine interpolation coefficients
	Interpolate_2D_Bilinear(P, T, ThermoTables_Pressure,
			ThermoTables_Temperature, "PT");
	//Interpolate the fluid properties
	Pressure = P;
	Temperature = T;
	Density = Interpolate_2D_Bilinear(ThermoTables_Density);
	StaticEnergy = Interpolate_2D_Bilinear(ThermoTables_StaticEnergy);
	Enthalpy = Interpolate_2D_Bilinear(ThermoTables_Enthalpy);
	Entropy = Interpolate_2D_Bilinear(ThermoTables_Entropy);
	SoundSpeed2 = Interpolate_2D_Bilinear(ThermoTables_SoundSpeed2);
	dPdrho_e = Interpolate_2D_Bilinear(ThermoTables_dPdrho_e);
	dPde_rho = Interpolate_2D_Bilinear(ThermoTables_dPde_rho);
	dTdrho_e = Interpolate_2D_Bilinear(ThermoTables_dTdrho_e);
	dTde_rho = Interpolate_2D_Bilinear(ThermoTables_dTde_rho);
	Cp = Interpolate_2D_Bilinear(ThermoTables_Cp);
	Mu = Interpolate_2D_Bilinear(ThermoTables_Mu);
	Kt = Interpolate_2D_Bilinear(ThermoTables_Kt);

}

void CLookUpTable::SetTDState_Prho(su2double P, su2double rho) {

	Get_Current_Points_From_TrapezoidalMap(Prho_map, P, rho);

	//Determine interpolation coefficients
	Interpolate_2D_Bilinear(rho, P, ThermoTables_Density,
			ThermoTables_Pressure, "PRHO");
//Interpolate the fluid properties
	Pressure = P;
	Density = rho;
	StaticEnergy = Interpolate_2D_Bilinear(ThermoTables_StaticEnergy);
	Enthalpy = Interpolate_2D_Bilinear(ThermoTables_Enthalpy);
	Entropy = Interpolate_2D_Bilinear(ThermoTables_Entropy);
	SoundSpeed2 = Interpolate_2D_Bilinear(ThermoTables_SoundSpeed2);
	Temperature = Interpolate_2D_Bilinear(ThermoTables_Temperature);
	dPdrho_e = Interpolate_2D_Bilinear(ThermoTables_dPdrho_e);
	dPde_rho = Interpolate_2D_Bilinear(ThermoTables_dPde_rho);
	dTdrho_e = Interpolate_2D_Bilinear(ThermoTables_dTdrho_e);
	dTde_rho = Interpolate_2D_Bilinear(ThermoTables_dTde_rho);
	Cp = Interpolate_2D_Bilinear(ThermoTables_Cp);
	Mu = Interpolate_2D_Bilinear(ThermoTables_Mu);
	Kt = Interpolate_2D_Bilinear(ThermoTables_Kt);

}

void CLookUpTable::SetEnergy_Prho(su2double P, su2double rho) {

	Get_Current_Points_From_TrapezoidalMap(Prho_map, P, rho);

//Determine interpolation coefficients
	Interpolate_2D_Bilinear(rho, P, ThermoTables_Density,
			ThermoTables_Pressure, "PRHO");
	StaticEnergy = Interpolate_2D_Bilinear(ThermoTables_StaticEnergy);
	Pressure = P;
	Density = rho;

}

void CLookUpTable::SetTDState_hs(su2double h, su2double s) {

	Get_Current_Points_From_TrapezoidalMap(hs_map, h, s);

//Determine interpolation coefficients
	Interpolate_2D_Bilinear(h, s, ThermoTables_Enthalpy,
			ThermoTables_Entropy, "HS");

//Interpolate the fluid properties
	Enthalpy = h;
	Entropy = s;
	StaticEnergy = Interpolate_2D_Bilinear(ThermoTables_StaticEnergy);
	Pressure = Interpolate_2D_Bilinear(ThermoTables_Pressure);
	Density = Interpolate_2D_Bilinear(ThermoTables_Density);
	SoundSpeed2 = Interpolate_2D_Bilinear(ThermoTables_SoundSpeed2);
	Temperature = Interpolate_2D_Bilinear(ThermoTables_Temperature);
	dPdrho_e = Interpolate_2D_Bilinear(ThermoTables_dPdrho_e);
	dPde_rho = Interpolate_2D_Bilinear(ThermoTables_dPde_rho);
	dTdrho_e = Interpolate_2D_Bilinear(ThermoTables_dTdrho_e);
	dTde_rho = Interpolate_2D_Bilinear(ThermoTables_dTde_rho);
	Cp = Interpolate_2D_Bilinear(ThermoTables_Cp);
	Mu = Interpolate_2D_Bilinear(ThermoTables_Mu);
	Kt = Interpolate_2D_Bilinear(ThermoTables_Kt);

}

void CLookUpTable::SetTDState_Ps(su2double P, su2double s) {

	Get_Current_Points_From_TrapezoidalMap(Ps_map, P, s);

//Determine interpolation coefficients
	Interpolate_2D_Bilinear(s, P, ThermoTables_Entropy,
			ThermoTables_Pressure, "PS");

//Interpolate the fluid properties
	Entropy = s;
	Pressure = P;
	StaticEnergy = Interpolate_2D_Bilinear(ThermoTables_StaticEnergy);
	Enthalpy = Interpolate_2D_Bilinear(ThermoTables_Enthalpy);
	Density = Interpolate_2D_Bilinear(ThermoTables_Density);
	SoundSpeed2 = Interpolate_2D_Bilinear(ThermoTables_SoundSpeed2);
	Temperature = Interpolate_2D_Bilinear(ThermoTables_Temperature);
	dPdrho_e = Interpolate_2D_Bilinear(ThermoTables_dPdrho_e);
	dPde_rho = Interpolate_2D_Bilinear(ThermoTables_dPde_rho);
	dTdrho_e = Interpolate_2D_Bilinear(ThermoTables_dTdrho_e);
	dTde_rho = Interpolate_2D_Bilinear(ThermoTables_dTde_rho);
	Cp = Interpolate_2D_Bilinear(ThermoTables_Cp);
	Mu = Interpolate_2D_Bilinear(ThermoTables_Mu);
	Kt = Interpolate_2D_Bilinear(ThermoTables_Kt);

}

void CLookUpTable::SetTDState_rhoT(su2double rho, su2double T) {

	Get_Current_Points_From_TrapezoidalMap(rhoT_map, rho, T);
//Determine the interpolation coefficients
	Interpolate_2D_Bilinear(rho, T, ThermoTables_Density,
			ThermoTables_Temperature, "RHOT");

//Interpolate the fluid properties
	Temperature = T;
	Density = rho;
	StaticEnergy = Interpolate_2D_Bilinear(ThermoTables_StaticEnergy);
	Enthalpy = Interpolate_2D_Bilinear(ThermoTables_Enthalpy);
	Entropy = Interpolate_2D_Bilinear(ThermoTables_Entropy);
	Pressure = Interpolate_2D_Bilinear(ThermoTables_Pressure);
	SoundSpeed2 = Interpolate_2D_Bilinear(ThermoTables_SoundSpeed2);
	dPdrho_e = Interpolate_2D_Bilinear(ThermoTables_dPdrho_e);
	dPde_rho = Interpolate_2D_Bilinear(ThermoTables_dPde_rho);
	dTdrho_e = Interpolate_2D_Bilinear(ThermoTables_dTdrho_e);
	dTde_rho = Interpolate_2D_Bilinear(ThermoTables_dTde_rho);
	Cp = Interpolate_2D_Bilinear(ThermoTables_Cp);
	Mu = Interpolate_2D_Bilinear(ThermoTables_Mu);
	Kt = Interpolate_2D_Bilinear(ThermoTables_Kt);

}

void CLookUpTable::Interpolate_2D_Bilinear(su2double x,
		su2double y, vector<su2double> *ThermoTables_X,
		vector<su2double> *ThermoTables_Y, std::string grid_var) {
	//The x,y coordinates of the quadrilateral
	su2double x0, y0, x1, x2, y1, y2;
	sort(CurrentPoints.begin(), CurrentPoints.end());
	vector<int>::iterator iter = unique(CurrentPoints.begin(),
			CurrentPoints.end());
	CurrentPoints.resize(distance(CurrentPoints.begin(), iter));

	x0 = ThermoTables_X[CurrentZone][CurrentPoints[0]];
	y0 = ThermoTables_Y[CurrentZone][CurrentPoints[0]];
	x1 = ThermoTables_X[CurrentZone][CurrentPoints[1]];
	y1 = ThermoTables_Y[CurrentZone][CurrentPoints[1]];
	x2 = ThermoTables_X[CurrentZone][CurrentPoints[2]];
	y2 = ThermoTables_Y[CurrentZone][CurrentPoints[2]];

	//Setup the LHM matrix for the interpolation (Vandermonde)
	Interpolation_Matrix[0][0] = 1;
	Interpolation_Matrix[0][1] = 0;
	Interpolation_Matrix[0][2] = 0;

	Interpolation_Matrix[1][0] = 1;
	Interpolation_Matrix[1][1] = x1 - x0;
	Interpolation_Matrix[1][2] = y1 - y0;

	Interpolation_Matrix[2][0] = 1;
	Interpolation_Matrix[2][1] = x2 - x0;
	Interpolation_Matrix[2][2] = y2 - y0;

	//Invert the Interpolation matrix and take the transpose
	Interpolation_Coeff[0][0] = 1;
	Interpolation_Coeff[0][1] = (y1 - y2)
			/ (x0 * y1 - x0 * y2 - x1 * y0 + x1 * y2 + x2 * y0 - x2 * y1);
	Interpolation_Coeff[0][2] = (-x1 + x2)
			/ (x0 * y1 - x0 * y2 - x1 * y0 + x1 * y2 + x2 * y0 - x2 * y1);
	Interpolation_Coeff[1][0] = 0;
	Interpolation_Coeff[1][1] = (-y0 + y2)
			/ (x0 * y1 - x0 * y2 - x1 * y0 + x1 * y2 + x2 * y0 - x2 * y1);
	Interpolation_Coeff[1][2] = (x0 - x2)
			/ (x0 * y1 - x0 * y2 - x1 * y0 + x1 * y2 + x2 * y0 - x2 * y1);
	Interpolation_Coeff[2][0] = 0;
	Interpolation_Coeff[2][1] = (y0 - y1)
			/ (x0 * y1 - x0 * y2 - x1 * y0 + x1 * y2 + x2 * y0 - x2 * y1);
	Interpolation_Coeff[2][2] = (-x0 + x1)
			/ (x0 * y1 - x0 * y2 - x1 * y0 + x1 * y2 + x2 * y0 - x2 * y1);

	//The transpose allows the same coefficients to be used
	// for all Thermo variables (need only 4 coefficients)
	su2double d;
	for (int i = 0; i < 3; i++) {
		d = 0;
		d = d + Interpolation_Coeff[i][0] * 1;
		d = d + Interpolation_Coeff[i][1] * (x - x0);
		d = d + Interpolation_Coeff[i][2] * (y - y0);
		Interpolation_Coeff[i][0] = d;
	}
	return;
}

su2double CLookUpTable::Interpolate_2D_Bilinear(
		vector<su2double> *ThermoTables_Z) {
//The function values at the 4 corners of the quad
	su2double func_value_1, func_value_2, func_value_3;

	func_value_1 = ThermoTables_Z[CurrentZone][CurrentPoints[0]];
	func_value_2 = ThermoTables_Z[CurrentZone][CurrentPoints[1]];
	func_value_3 = ThermoTables_Z[CurrentZone][CurrentPoints[2]];

//The Interpolation_Coeff values depend on location alone
//and are the same regardless of function values
	su2double result = 0;
	result = result + Interpolation_Coeff[0][0] * func_value_1;
	result = result + Interpolation_Coeff[1][0] * func_value_2;
	result = result + Interpolation_Coeff[2][0] * func_value_3;

	return result;
}

void CLookUpTable::RecordState(char* file) {
//Record the state of the fluid model to a file for
//verificaiton purposes
	fstream fs;
	fs.open(file, fstream::app);
	fs.precision(17);
	assert(fs.is_open());
	fs << Temperature << ", ";
	fs << Density << ", ";
	fs << Enthalpy << ", ";
	fs << StaticEnergy << ", ";
	fs << Entropy << ", ";
	fs << Pressure << ", ";
	fs << SoundSpeed2 << ", ";
	fs << dPdrho_e << ", ";
	fs << dPde_rho << ", ";
	fs << dTdrho_e << ", ";
	fs << dTde_rho << ", ";
	fs << Cp << ", ";
	fs << Mu << ", ";
//fs << dmudrho_T << ", ";
//fs << dmudT_rho << ", ";
	fs << Kt << " ";
//fs << dktdrho_T << ", ";
//fs << dktdT_rho << ", ";
	fs << "\n";
	fs.close();
}

void CLookUpTable::LookUpTable_Print_To_File(char* filename) {
//Print the entire table to a file such that the mesh can be plotted
//externally (for verification purposes)
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < nTable_Zone_Stations[i]; j++) {
			Temperature = ThermoTables_Temperature[i][j];
			Density = ThermoTables_Density[i][j];
			Enthalpy = ThermoTables_Enthalpy[i][j];
			StaticEnergy = ThermoTables_StaticEnergy[i][j];
			Entropy = ThermoTables_Entropy[i][j];
			Pressure = ThermoTables_Pressure[i][j];
			SoundSpeed2 = ThermoTables_SoundSpeed2[i][j];
			dPdrho_e = ThermoTables_dPdrho_e[i][j];
			dPde_rho = ThermoTables_dPde_rho[i][j];
			dTdrho_e = ThermoTables_dTdrho_e[i][j];
			dTde_rho = ThermoTables_dTde_rho[i][j];
			Cp = ThermoTables_Cp[i][j];
			Kt = ThermoTables_Kt[i][j];
			Mu = ThermoTables_Mu[i][j];
			RecordState(filename);
		}
	}

}

void CLookUpTable::LookUpTable_Load_TEC(std::string filename) {
	string line;
	string value;
	int found;
	int zone_scanned;

	ifstream table(filename.c_str());
	if (!table.is_open()) {
		if (rank == MASTER_NODE) {
			cout << "The LUT file appears to be missing!! " << filename << endl;
		}
		exit(EXIT_FAILURE);
	}
	zone_scanned = 0;
//Go through all lines in the table file.
	getline(table, line);	//Skip the header
	while (getline(table, line)) {
		found = line.find("ZONE");
		if (found != -1) {
			if (rank == MASTER_NODE and LUT_Debug_Mode)
			{cout << line << endl;}
			istringstream in(line);
//Note down the dimensions of the table
			int nPoints_in_Zone, nTriangles_in_Zone;
			string c1, c2, c3, c4;
			in >> c1 >> c2 >> nPoints_in_Zone >> c3 >> c4 >> nTriangles_in_Zone;
			if (rank == MASTER_NODE) cout << nPoints_in_Zone << "  " << nTriangles_in_Zone << endl;
//Create the actual LUT of CThermoLists which is used in the FluidModel
			nTable_Zone_Stations[zone_scanned] = nPoints_in_Zone;
			nTable_Zone_Triangles[zone_scanned] = nTriangles_in_Zone;
//Allocate the memory for the table
			LookUpTable_Malloc(zone_scanned);

//Load the values of the themordynamic properties at each table station
			for (int j = 0; j < nTable_Zone_Stations[zone_scanned]; j++) {
				getline(table, line);
				istringstream in(line);
				in >> ThermoTables_Density[zone_scanned][j];
				in >> ThermoTables_Pressure[zone_scanned][j];
				in >> ThermoTables_SoundSpeed2[zone_scanned][j];
				in >> ThermoTables_Cp[zone_scanned][j];
				in >> ThermoTables_Entropy[zone_scanned][j];
				in >> ThermoTables_Mu[zone_scanned][j];
				in >> ThermoTables_Kt[zone_scanned][j];
				in >> ThermoTables_dPdrho_e[zone_scanned][j];
				in >> ThermoTables_dPde_rho[zone_scanned][j];
				in >> ThermoTables_dTdrho_e[zone_scanned][j];
				in >> ThermoTables_dTde_rho[zone_scanned][j];
				in >> ThermoTables_Temperature[zone_scanned][j];
				in >> ThermoTables_StaticEnergy[zone_scanned][j];
				in >> ThermoTables_Enthalpy[zone_scanned][j];
			}
//Skip empty line
			getline(table, line);
//Load the triangles i.e. how the data point in each zone are connected
			for (int j = 0; j < nTable_Zone_Triangles[zone_scanned]; j++) {
				getline(table, line);
				istringstream in(line);
				in >> Table_Zone_Triangles[zone_scanned][j][0]
						>> Table_Zone_Triangles[zone_scanned][j][1]
						>> Table_Zone_Triangles[zone_scanned][j][2];
				//Triangles in .tec file are indexed from 1
				//In cpp it is more convenient to start with 0.
				Table_Zone_Triangles[zone_scanned][j][0]--;
				Table_Zone_Triangles[zone_scanned][j][1]--;
				Table_Zone_Triangles[zone_scanned][j][2]--;
			}

			zone_scanned++;
		}
	}

	table.close();
//NonDimensionalise
	NonDimensionalise_Table_Values();
}

void CLookUpTable::LookUpTable_Malloc(int Index_of_Zone) {
	ThermoTables_StaticEnergy[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Entropy[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Enthalpy[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Density[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Pressure[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_SoundSpeed2[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Temperature[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_dPdrho_e[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_dPde_rho[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_dTdrho_e[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_dTde_rho[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Cp[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Mu[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	//ThermoTables_dmudrho_T[Index_of_Zone] = vector< su2double >(
	//	nTable_Zone_Stations[Index_of_Zone], 0);
	//ThermoTables_dmudT_rho[Index_of_Zone] = vector< su2double >(
	//	nTable_Zone_Stations[Index_of_Zone], 0);
	ThermoTables_Kt[Index_of_Zone] = vector<su2double>(
			nTable_Zone_Stations[Index_of_Zone], 0);
	//ThermoTables_dktdrho_T[Index_of_Zone] = vector< su2double >(
	//		nTable_Zone_Stations[Index_of_Zone], 0);
	//ThermoTables_dktdT_rho[Index_of_Zone] = vector< su2double >(
	//		nTable_Zone_Stations[Index_of_Zone], 0);
	Table_Zone_Triangles[Index_of_Zone] = vector<vector<int> >(
			nTable_Zone_Triangles[Index_of_Zone]);
	for (int j = 0; j < nTable_Zone_Triangles[Index_of_Zone]; j++) {
		Table_Zone_Triangles[Index_of_Zone][j] = vector<int>(3, 0);
	}
}

void CLookUpTable::NonDimensionalise_Table_Values() {
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < nTable_Zone_Stations[i]; j++) {
			ThermoTables_Density[i][j] /= Density_Reference_Value;
			ThermoTables_Pressure[i][j] /= Pressure_Reference_Value;
			ThermoTables_SoundSpeed2[i][j] = pow(ThermoTables_SoundSpeed2[i][j], 2);
			ThermoTables_SoundSpeed2[i][j] /= pow(Velocity_Reference_Value, 2);
			ThermoTables_Cp[i][j] *= (Temperature_Reference_Value
					/ Energy_Reference_Value);
			ThermoTables_Entropy[i][j] *= (Temperature_Reference_Value
					/ Energy_Reference_Value);
			ThermoTables_dPdrho_e[i][j] *= (Density_Reference_Value
					/ Pressure_Reference_Value);
			ThermoTables_dPde_rho[i][j] *= (Energy_Reference_Value
					/ Pressure_Reference_Value);
			ThermoTables_dTdrho_e[i][j] *= (Density_Reference_Value
					/ Temperature_Reference_Value);
			ThermoTables_dTde_rho[i][j] *= (Energy_Reference_Value
					/ Temperature_Reference_Value);
			ThermoTables_Temperature[i][j] /= Temperature_Reference_Value;
			ThermoTables_StaticEnergy[i][j] /= Energy_Reference_Value;
			ThermoTables_Enthalpy[i][j] /= Energy_Reference_Value;
		}
	}
}
