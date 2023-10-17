/* Copyright (c) 2013-2023, EPFL/Blue Brain Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "morphologySWC.h"
#include "utils.h"

#include <cctype>         // isdigit
#include <cstdint>        // uint32_t
#include <memory>         // std::shared_ptr
#include <string>         // std::string
#include <unordered_map>  // std::unordered_map
#include <vector>         // std::vector

#include <morphio/errorMessages.h>
#include <morphio/mut/morphology.h>
#include <morphio/mut/section.h>
#include <morphio/mut/soma.h>
#include <morphio/properties.h>

namespace details {
using namespace morphio;

// It's not clear if -1 is the only way of identifying a root section.
const int SWC_UNDEFINED_PARENT = -1;
const unsigned int SWC_ROOT = 0xFFFFFFFD;

/* simple stream parser for SWC file format which is a line oriented format
 *
 * This parser advances across comments and blank lines, and allows the caller
 * to get integers and floats
 */
class SWCTokenizer
{
public:
  explicit SWCTokenizer(std::string contents, const morphio::readers::ErrorMessages& err)
      : contents_(std::move(contents))
      , err_(err) {
      // ensure null termination
      (void) contents_.c_str();
  }

  bool done() const noexcept {
      return pos_ >= contents_.size();
  }

  size_t lineNumber() const noexcept {
      return line_;
  }

  void skip_to(char value) {
      std::size_t pos = contents_.find_first_of(value, pos_);
      if (pos == std::string::npos) {
          pos_ = contents_.size();
      }
      pos_ = pos;
  }

  void advance_to_non_whitespace() {
      if (done()) {
          return;
      }
      std::size_t pos = contents_.find_first_not_of(" \t\r", pos_);
      if (pos == std::string::npos) {
          pos_ = contents_.size();
          return;
      }
      pos_ = pos;
  }

  void advance_to_number() {
      while (!done() && consume_line_and_trailing_comments()) {
      }

      if (done()) {
          throw morphio::RawDataError(err_.EARLY_END_OF_FILE(line_));
      }

      auto c = contents_.at(pos_);
      if (std::isdigit(c) != 0 || c == '-' || c == '+' || c == '.') {
          return;
      }

      throw morphio::RawDataError(err_.ERROR_LINE_NON_PARSABLE(line_));
  }

  int64_t read_int() {
      advance_to_number();
      auto parsed = stn_.toInt(contents_, pos_);
      pos_ = std::get<1>(parsed);
      return std::get<0>(parsed);
  }

  morphio::floatType read_float() {
      advance_to_number();
      auto parsed = stn_.toFloat(contents_, pos_);
      pos_ = std::get<1>(parsed);
      return std::get<0>(parsed);
  }

  bool consume_line_and_trailing_comments() {
      bool found_newline = false;

      advance_to_non_whitespace();
      while (!done() && (contents_.at(pos_) == '#' || contents_.at(pos_) == '\n')) {
          switch (contents_.at(pos_)) {
          case '#':
              skip_to('\n');
              break;
          case '\n':
              ++line_;
              ++pos_;
              found_newline = true;
              break;
          }
          advance_to_non_whitespace();
      }
      return found_newline || done();
  }

private:
  size_t pos_ = 0;
  size_t line_ = 1;
  std::string contents_;
  morphio::StringToNumber stn_{};
  morphio::readers::ErrorMessages err_;
};

std::vector<morphio::readers::Sample> readSamples(const std::string& contents,
                                                  const morphio::readers::ErrorMessages& err) {
    std::vector<morphio::readers::Sample> samples;
    morphio::readers::Sample sample;

    SWCTokenizer tokenizer{contents, err};
    tokenizer.consume_line_and_trailing_comments();

    while (!tokenizer.done()) {
        sample.lineNumber = static_cast<unsigned int>(tokenizer.lineNumber());

        int64_t id = tokenizer.read_int();
        if (id < 0) {
            throw morphio::RawDataError(err.ERROR_NEGATIVE_ID(sample.lineNumber));
        }

        sample.id = static_cast<unsigned int>(id);

        sample.type = static_cast<morphio::SectionType>(tokenizer.read_int());

        for (auto& point : sample.point) {
            point = tokenizer.read_float();
        }

        sample.diameter = 2 * tokenizer.read_float();

        int64_t parentId = tokenizer.read_int();
        if (parentId < -1) {
            throw morphio::RawDataError(err.ERROR_NEGATIVE_ID(sample.lineNumber));
        } else if (parentId == SWC_UNDEFINED_PARENT) {
            sample.parentId = SWC_ROOT;
        } else {
            sample.parentId = static_cast<unsigned int>(parentId);
        }

        if (!tokenizer.consume_line_and_trailing_comments()) {
            throw morphio::RawDataError(err.ERROR_LINE_NON_PARSABLE(sample.lineNumber));
        }
        samples.push_back(sample);
    }
    return samples;
}

/**
  Parsing SWC according to this specification:
http://www.neuronland.org/NLMorphologyConverter/MorphologyFormats/SWC/Spec.html
 **/
using morphio::readers::ErrorMessages;
using morphio::readers::Sample;

enum class DeclaredID : unsigned int {};

class SWCBuilder
{
    using MorphioID = uint32_t;
    using PhysicalID = size_t;
    using Samples = std::vector<Sample>;

  public:
    explicit SWCBuilder(const std::string& path)
        : err_(path) {}

    Property::Properties buildProperties(const std::string& contents, unsigned int options) {
        const Samples samples = readSamples(contents, err_);
        buildSWC(samples);
        morph_.applyModifiers(options);
        return morph_.buildReadOnly();
    }

  private:
    void _checkNeuromorph3PointSoma(const Samples& soma_samples){
        // First point is the 'center'; has 2 children
        const Sample& center = soma_samples[0];
        const Sample& child1 = soma_samples[1];
        const Sample& child2 = soma_samples[2];

        floatType x = center.point[0];
        floatType z = center.point[2];
        floatType d = center.diameter;
        floatType r = center.diameter / 2;
        floatType y = center.point[1];

        // whether the soma should be checked for the special case of 3 point soma
        // for details see https://github.com/BlueBrain/MorphIO/issues/273
        // If the 2nd and the 3rd point have the same x, z, d values then the only valid soma
        // is: 1 1 x   y   z r -1 2 1 x (y-r) z r  1 3 1 x (y+r) z r  1
        if ((child1.point[0] != x || child2.point[0] != x || child1.point[1] != y - r ||
             child2.point[1] != y + r || child1.point[2] != z || child2.point[2] != z ||
             child1.diameter != d || child2.diameter != d) &&
            std::fabs(child1.diameter - d) < morphio::epsilon &&
            std::fabs(child2.diameter - d) < morphio::epsilon &&
            std::fabs(child1.point[0] - x) < morphio::epsilon &&
            std::fabs(child2.point[0] - x) < morphio::epsilon &&
            std::fabs(child1.point[2] - z) < morphio::epsilon &&
            std::fabs(child2.point[2] - z) < morphio::epsilon
            ) {
            printError(Warning::SOMA_NON_CONFORM,
                       err_.WARNING_NEUROMORPHO_SOMA_NON_CONFORM(center, child1, child2));
        }
    }

    void build_soma(const Samples& soma_samples) {
        auto& soma = morph_.soma();

        if (soma_samples.empty()) {
            soma->type() = SOMA_UNDEFINED;
            return;
        } else if (soma_samples.size() == 1) {
            Sample sample = soma_samples[0];

            if (sample.parentId != SWC_ROOT && samples_.at(sample.parentId).type != SECTION_SOMA) {
                throw morphio::SomaError(err_.ERROR_SOMA_WITH_NEURITE_PARENT(sample));
            }

            soma->type() = SOMA_SINGLE_POINT;
            soma->points() = {sample.point};
            soma->diameters() = {sample.diameter};
            return;
        } else if (soma_samples.size() == 3) {
            const Sample& center = soma_samples[0];
            const Sample& child1 = soma_samples[1];
            const Sample& child2 = soma_samples[2];
            // All soma that bifurcate with the first parent having two children are considered
            // SOMA_NEUROMORPHO_THREE_POINT_CYLINDERS
            if(center.id == child1.parentId && center.id == child2.parentId){
                soma->type() = SOMA_NEUROMORPHO_THREE_POINT_CYLINDERS;
                soma->points() = {center.point, child1.point, child2.point};
                soma->diameters() = {center.diameter, child1.diameter, child2.diameter};
                _checkNeuromorph3PointSoma(soma_samples);
               return;
           }
        }
        // might also have 3 points at this point, as well

        // a "normal" SWC soma
        soma->type() = SOMA_CYLINDERS;
        auto& points = soma->points();
        auto& diameters = soma->diameters();
        points.reserve(soma_samples.size());
        diameters.reserve(soma_samples.size());

        size_t parent_count = 0;
        for (const auto& s : soma_samples) {
            if (s.parentId == SWC_ROOT) {
                parent_count++;
            } else if (samples_.count(s.parentId) == 0) {
                throw morphio::MissingParentError(err_.ERROR_MISSING_PARENT(s));
            } else if (samples_.at(s.parentId).type != SECTION_SOMA) {
                throw morphio::SomaError(err_.ERROR_SOMA_WITH_NEURITE_PARENT(s));
            }

            if (children_.count(s.id) > 0 && children_.at(s.id).size() > 1) {
                std::vector<Sample> soma_bifurcations;
                for (auto id : children_.at(s.id)) {
                    if (samples_[id].type == SECTION_SOMA) {
                        soma_bifurcations.push_back(samples_[id]);
                    }
                }
                if (soma_bifurcations.size() > 1) {
                    throw morphio::SomaError(err_.ERROR_SOMA_BIFURCATION(s, soma_bifurcations));
                }
            }
            points.push_back(s.point);
            diameters.push_back(s.diameter);
        }

        if (parent_count > 1) {
            throw morphio::SomaError(err_.ERROR_MULTIPLE_SOMATA(soma_samples));
        }
    }

    void buildSWC(const Samples& samples) {
        Samples soma_samples;
        Samples root_samples;

        for (const auto& sample: samples) {
            // { checks
            if (sample.diameter < morphio::epsilon) {
                printError(Warning::ZERO_DIAMETER, err_.WARNING_ZERO_DIAMETER(sample));
            }

            if (sample.parentId == sample.id) {
                throw morphio::RawDataError(err_.ERROR_SELF_PARENT(sample));
            }

            if (sample.type >= morphio::SECTION_OUT_OF_RANGE_START || sample.type <= 0) {
                throw morphio::RawDataError(
                    err_.ERROR_UNSUPPORTED_SECTION_TYPE(sample.lineNumber, sample.type));
            }
            if (sample.parentId == SWC_ROOT && sample.type != SECTION_SOMA) {
                printError(Warning::DISCONNECTED_NEURITE,
                           err_.WARNING_DISCONNECTED_NEURITE(sample));
            }
            // } checks

            if (sample.type == SECTION_SOMA) {
                soma_samples.push_back(sample);
            }

            if (sample.parentId == SWC_ROOT || sample.type == SECTION_SOMA) {
                root_samples.push_back(sample);
            }

            if (!samples_.insert({sample.id, sample}).second) {
                throw RawDataError(err_.ERROR_REPEATED_ID(samples[sample.id], sample));
            }

            children_[sample.parentId].push_back(sample.id);
        }

        // can only check for missing parents once all samples are loaded
        // since it's possible there may be forward references
        for (const auto& sample: samples) {
            if(sample.parentId != SWC_ROOT && samples_.count(sample.parentId) == 0){
                throw morphio::MissingParentError(err_.ERROR_MISSING_PARENT(sample));
            }
        }

        build_soma(soma_samples);

        std::unordered_map<DeclaredID, std::shared_ptr<morphio::mut::Section>> declared_to_swc;
        declared_to_swc.reserve(samples.size());

        for (const Sample& root_sample : root_samples) {
            if (children_.count(root_sample.id) == 0) {
                continue;
            }

            // https://neuromorpho.org/SomaFormat.html
            // "The second and third soma points, as well as all starting points
            // (roots) of dendritic and axonal arbors have this first point as
            // the parent (parent ID 1)."
            if (morph_.soma()->type() == SOMA_NEUROMORPHO_THREE_POINT_CYLINDERS &&
                root_sample.type == SECTION_SOMA && root_sample.id != 1) {
                printError(Warning::WRONG_ROOT_POINT, err_.WARNING_WRONG_ROOT_POINT(root_sample));
            }

            for (unsigned int child_id : children_.at(root_sample.id)) {
                if (samples_.at(child_id).type == SECTION_SOMA) {
                    continue;
                }
                if (root_sample.type == SECTION_SOMA) {
                    assembleSections(child_id,
                                     DeclaredID(root_sample.id),
                                     declared_to_swc,
                                     morph_.soma()->points()[0],
                                     morph_.soma()->diameters()[0],
                                     true);
                } else {
                    // this is neurite as the start
                    assembleSections(root_sample.id,
                                     DeclaredID(SWC_ROOT),
                                     declared_to_swc,
                                     root_sample.point,
                                     root_sample.diameter,
                                     true);
                    break;
                }
            }
        }
    }

    void assembleSections(
        unsigned int id,
        DeclaredID parent_id,
        std::unordered_map<DeclaredID, std::shared_ptr<morphio::mut::Section>>& declared_to_swc,
        const Point& start_point,
        floatType start_diameter,
        bool is_root) {

        morphio::Property::PointLevel properties;
        auto& points = properties._points;
        auto& diameters = properties._diameters;

        auto appendSection = [&](DeclaredID section_id_, DeclaredID parent_id_, SectionType starting_section_type) {
            std::shared_ptr<morphio::mut::Section> new_section;
            if (is_root) {
                new_section = morph_.appendRootSection(properties, starting_section_type);
            } else {
                new_section = declared_to_swc.at(parent_id_)
                                  ->appendSection(properties, starting_section_type);
            }
            declared_to_swc[section_id_] = new_section;
        };

        auto get_child_count = [&](unsigned int child_id) {
            return children_.count(child_id) == 0 ? 0 : children_.at(child_id).size();
        };

        const Sample* sample = &samples_.at(id);

        // create duplicate point if needed
        if (!is_root && sample->point != start_point /*|| sample->diameter != start_diameter */) {
        //if (!(is_root && sample->type == SECTION_SOMA) && sample->point != start_point /*|| sample->diameter != start_diameter */) {
            points.push_back(start_point);
            diameters.push_back(start_diameter);
        }

        // try and combine as many single samples into a single section as possible
        size_t children_count = get_child_count(id);
        while (children_count == 1) {
            sample = &samples_.at(id);
            if(sample->type != samples_.at(children_.at(id)[0]).type){
                break;
            }
            points.push_back(sample->point);
            diameters.push_back(sample->diameter);
            id = children_.at(id)[0];
            children_count = get_child_count(id);
        }
        sample = &samples_.at(id);
        points.push_back(sample->point);
        diameters.push_back(sample->diameter);
        appendSection(DeclaredID(id), parent_id, sample->type);

        if (children_count == 0) {
            // section was already appended above, nothing to do
        } else if (children_count == 1) {
            // section_type changed
            size_t offset = properties._points.size() - 1;
            const Point& new_start_point = properties._points[offset];
            floatType new_start_diameter = properties._diameters[offset];
            assembleSections(children_.at(id)[0], DeclaredID(id), declared_to_swc, new_start_point, new_start_diameter, false);
        } else {
            size_t offset = properties._points.size() - 1;
            const Point& new_start_point = properties._points[offset];
            floatType new_start_diameter = properties._diameters[offset];
            for (unsigned int child_id : children_.at(id)) {
                assembleSections(child_id,
                                 DeclaredID(id),
                                 declared_to_swc,
                                 new_start_point,
                                 new_start_diameter,
                                 false);
            }
        }
    }

    std::unordered_map<unsigned int, std::vector<unsigned int>> children_;
    std::unordered_map<unsigned int, Sample> samples_;
    mut::Morphology morph_;
    ErrorMessages err_;
};


}  // namespace details

namespace morphio {
namespace readers {
namespace swc {
Property::Properties load(const std::string& path,
                          const std::string& contents,
                          unsigned int options) {
    auto properties = details::SWCBuilder(path).buildProperties(contents, options);

    properties._cellLevel._cellFamily = NEURON;
    properties._cellLevel._version = {"swc", 1, 0};
    return properties;
}

}  // namespace swc
}  // namespace readers
}  // namespace morphio
