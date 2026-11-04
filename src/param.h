#pragma once

// SAFETY HEADWAY TIME (time units). Used to add incompatibility for a short
// interval after departures/arrivals.
const int t_h = 5;
// Cost model (see Arc::Arc in src/Arc.cpp): cost = w * duration + c
// Shunting arcs: w = train-type w_shu, c = c_shu (fixed per shunting arc).
// Storage/Parking arcs: w = w_dwell (fixed per time unit), c = c_park (fixed per storage arc).
// Dwelling arcs: w = w_dwell, c = 0, duration = t_s (one time step).
const int c_shu = 2;   // SHUNTING fixed add-on per shunting arc
const int w_dwell = 1; // DWELLING weight per time unit
const int c_park = 5;  // PARKING fixed add-on per storage arc
const int w_shu = 2;   //Shunting weight that gives a cost proportional to the duration of shunting used to be defined in the network_builder for some reason

