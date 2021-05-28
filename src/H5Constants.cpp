#include "H5Constants.h"

namespace morphio {
namespace h5 {

//{v1
const std::string& d_structure() {
    static std::string v("structure");
    return v;
}

const std::string& d_points() {
    static std::string v("points");
    return v;
}

//{ v1.1
const std::string& a_version() {
    static std::string v("version");
    return v;
}

const std::string& g_metadata() {
    static std::string v("metadata");
    return v;
}

const std::string& a_family() {
    static std::string v("cell_family");
    return v;
}

const std::string& d_perimeters() {
    static std::string v("perimeters");
    return v;
}

//} v1.1

//{ v1.2
const std::string& g_mitochondria() {
    static std::string v("organelles/mitochondria");
    return v;
}


// endoplasmic reticulum
const std::string& g_endoplasmic_reticulum() {
    static std::string v("organelles/endoplasmic_reticulum");
    return v;
}

const std::string& d_section_index() {
    static std::string v("section_index");
    return v;
}

const std::string& d_volume() {
    static std::string v("volume");
    return v;
}

const std::string& d_surface_area() {
    static std::string v("surface_area");
    return v;
}

const std::string& d_filament_count() {
    static std::string v("filament_count");
    return v;
}

// } v1.2

// } v1

//{ v2
const std::string& g_v2root() {
    static std::string v("neuron1");
    return v;
}
//{ v2

}  // namespace h5
}  // namespace morphio
