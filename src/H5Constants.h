#include <cstddef>
#include <string>

namespace morphio {
namespace h5 {

constexpr size_t SECTION_START_OFFSET = 0;
constexpr size_t SECTION_TYPE = 1;
constexpr size_t SECTION_PARENT_OFFSET = 2;

//{v1
const std::string& d_structure();
const std::string& d_points();

//{ v1.1
const std::string& a_version();
const std::string& g_metadata();
const std::string& a_family();
const std::string& d_perimeters();
//} v1.1

//{ v1.2
const std::string& g_mitochondria();

// endoplasmic reticulum
const std::string& g_endoplasmic_reticulum();
const std::string& d_section_index();
const std::string& d_volume();
const std::string& d_surface_area();
const std::string& d_filament_count();
// } v1.2

// } v1

//{ v2
const std::string& g_v2root();
//} v2

}  // namespace h5
}  // namespace morphio
