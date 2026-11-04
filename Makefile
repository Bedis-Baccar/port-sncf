# Define the GUROBI_ENV variable in the .env file at the root of the of project
# i.e GUROBI_HOME=/path/to/gurobi/installation
include .env

SRC_DIR = src
OBJ_DIR = obj

TWOUP    = $(GUROBI_HOME)/include
INC      = $(TWOUP)/../include/

CXXFLAGS = -std=c++14 -m64 -g -I$(SRC_DIR) -I$(SRC_DIR)/header -I$(INC) -Wall -Wextra
CXX = g++
DBGFLAGS = -g -DAPM_DEBUG
LDFLAGS = -g
CPPLIB   = -L$(TWOUP)/../lib -lgurobi_c++ -lgurobi130

OBJ_RAW = input_reader P_Node P_Link Logger Node Arc Service Train Path network_builder path_builder Arc-path_formulation
OBJ_O = $(addsuffix .o,$(OBJ_RAW))
OBJ = $(addprefix $(OBJ_DIR)/,$(OBJ_O))

EXEC = tusp_solver

all: $(OBJ_DIR) $(EXEC)

$(OBJ_DIR):
	mkdir $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $^ $(CPPLIB) -lm

$(EXEC): $(OBJ) $(SRC_DIR)/tusp_column_generation.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(CPPLIB) -lm

report: Model.md
	pandoc --number-sections $< -o $@.pdf

clean:
	rm -rf $(EXEC) $(OBJ_DIR) report.pdf debug_logfile.log mip1.log
