#pragma once 

#include <vector>
#include <string>

#include "PNode.h"
#include "PLink.h"
#include "Train.h"
#include "Service.h"
#include "InstanceData.h"

class InstanceParser {
    private:
        // Chemin vers le dossier contenant les CSV de l'instance
        std::string directory_path_; 

        // Objet interne qui va être construit étape par étape
        InstanceData parsed_data_;
        
        int n_services_ = 0;
        int n_node_types_ = 0;
        int n_nodes_ = 0;
        int n_links_ = 0;
        int n_train_types_ = 0;
        int n_trains_ = 0;
        // Utilitaire privé pour lire un CSV spécifique
        std::vector<std::vector<std::string>> readCSV(const std::string& filepath) const;

        /* * Les méthodes suivantes lisent le fichier correspondant dans directory_path
         * et peuplent directement l'objet parsed_data.
         * Elles lèvent une std::runtime_error en cas de problème grave (ex: fichier introuvable).
         */
        void parse_services();
        void parse_node_types();
        void parse_nodes();
        void parse_links();
        void parse_train_types();
        void parse_trains();
        void parse_operations();

    public:
        explicit InstanceParser(const std::string& dir_path);

        // Orchestre l'appel à tous les parse_xxx() et retourne les données par valeur
        InstanceData parse();
};


