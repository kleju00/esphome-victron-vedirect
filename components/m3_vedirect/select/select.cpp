#include "select.h"
#include "esphome/core/application.h"
#if ESPHOME_VERSION_CODE < VERSION_CODE(2025, 11, 0)
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#else  // ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 11, 0)
#if defined(USE_CONTROLLER_REGISTRY)
#include "esphome/core/controller_registry.h"
#endif
#endif

#include "../manager.h"

#include <cinttypes>

namespace esphome {
namespace m3_vedirect {

#ifdef ESPHOME_LOG_HAS_DEBUG
static const char *const TAG = "m3_vedirect.select";
#endif

Register *Select::build_entity(Manager *manager, const REG_DEF *reg_def, const char *name) {
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 8, 0)
  if (App.get_selects().size() >= ESPHOME_ENTITY_SELECT_COUNT) {
    return Register::drop_platform(manager, Platform::Select);
  }
#endif
  auto entity = new Select(manager);
  manager->init_entity(entity, reg_def, name);
  App.register_select(entity);
#if ESPHOME_VERSION_CODE < VERSION_CODE(2025, 11, 0)
// See https://github.com/esphome/esphome/pull/11772
#if defined(USE_API)
  entity->add_on_state_callback([entity](const std::string &state, size_t index) {
    api::global_api_server->on_select_update(entity, state, index);
  });
#endif
#endif
  return entity;
}

void Select::link_disconnected_() {
  if (this->has_state()) {
    this->enum_value_ = ENUM_DEF::VALUE_UNKNOWN;
    this->publish_unknown_();
  }
}

void Select::init_reg_def_() {
  if (this->reg_def_->cls == REG_DEF::CLASS::ENUM) {
    std::vector<ENUM_DEF::LOOKUP_DEF> &lookups = this->reg_def_->enum_def->LOOKUPS;
    FixedVector<const char *> options;
    options.init(lookups.size());
    for (auto &lookup_def : lookups) {
      options.push_back(lookup_def.label);
    }
    this->traits.set_options(options);
#if defined(VEDIRECT_USE_HEXFRAME)
    this->parse_hex_ = parse_hex_enum_;
#endif
#if defined(VEDIRECT_USE_TEXTFRAME)
    this->parse_text_ = parse_text_enum_;
#endif
  }
}

void Select::parse_enum_(ENUM_DEF::enum_t enum_value) {
  if (this->enum_value_ != enum_value) {
    this->enum_value_ = enum_value;
    this->publish_enum_(enum_value);
  }
}

void Select::parse_string_(const char *string_value) {
  if (strcmp(this->current_option().c_str(), string_value)) {
    auto index = this->index_of(string_value);
    if (index.has_value()) {
      this->publish_index_(index.value());
      return;
    }
    const auto &options_old = this->traits.get_options();
    size_t new_index = options_old.size();
    FixedVector<const char *> options;
    options.init(new_index + 1);
    for (const auto &opt : options_old) {
      options.push_back(opt);
    }
    options.push_back(string_value);
    this->traits.set_options(options);
    this->publish_index_(new_index);
  }
}

#if defined(VEDIRECT_USE_HEXFRAME)
void Select::control(size_t index) {
  auto &lookups = this->reg_def_->enum_def->LOOKUPS;
  if (index < lookups.size()) {
    this->control_enum_(&lookups[index]);
  }
}

void Select::control(const std::string &value) {
  auto lookup_def = this->reg_def_->enum_def->lookup_value(value.c_str());
  if (lookup_def) {
    this->control_enum_(lookup_def);
  }
}

void Select::control_enum_(const ENUM_DEF::LOOKUP_DEF *lookup_def) {
  this->request_set_(lookup_def->value, [this](const HexFrame *frame, uint8_t error) {
    if (error) {
      if (this->enum_value_ == ENUM_DEF::VALUE_UNKNOWN) {
        this->publish_unknown_();
      } else {
        this->publish_enum_(this->enum_value_);
      }
    } else {
      this->enum_value_ = ENUM_DEF::VALUE_UNKNOWN;
    }
  });
}

void Select::parse_hex_default_(Register *hex_register, const RxHexFrame *hex_frame) {
  char hex_value[RxHexFrame::ALLOCATED_ENCODED_SIZE];
  if (hex_frame->data_to_hex(hex_value, RxHexFrame::ALLOCATED_ENCODED_SIZE)) {
    static_cast<Select *>(hex_register)->parse_string_(hex_value);
  }
}

void Select::parse_hex_enum_(Register *hex_register, const RxHexFrame *hex_frame) {
  static_assert(RxHexFrame::ALLOCATED_DATA_SIZE >= 1, "HexFrame storage might lead to access overflow");
  static_cast<Select *>(hex_register)->parse_enum_(hex_frame->data_t<ENUM_DEF::enum_t>());
}
#endif  // defined(VEDIRECT_USE_HEXFRAME)

#if defined(VEDIRECT_USE_TEXTFRAME)
void Select::parse_text_default_(Register *hex_register, const char *text_value) {
  static_cast<Select *>(hex_register)->parse_string_(text_value);
}

void Select::parse_text_enum_(Register *hex_register, const char *text_value) {
  char *endptr;
  ENUM_DEF::enum_t enum_value = strtoumax(text_value, &endptr, 0);
  if (*endptr == 0)
    static_cast<Select *>(hex_register)->parse_enum_(enum_value);
}
#endif  // defined(VEDIRECT_USE_TEXTFRAME)

void Select::publish_enum_(ENUM_DEF::enum_t enum_value) {
  auto enum_def = this->reg_def_->enum_def;
  auto lookup_result = enum_def->get_lookup(enum_value);
  const auto &current_options = this->traits.get_options();
  if (current_options.size() != enum_def->LOOKUPS.size()) {
    ESP_LOGD(TAG, "Rebuilding options (prev size %zu, new size %zu)", current_options.size(),
             enum_def->LOOKUPS.size());
    FixedVector<const char *> options;
    options.init(enum_def->LOOKUPS.size());
    for (auto &lookup_def : enum_def->LOOKUPS) {
      options.push_back(lookup_def.label);
    }
    this->traits.set_options(options);
  }
  this->publish_index_(lookup_result.index);
}

void Select::publish_index_(size_t index) {
  this->set_has_state(true);
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 11, 0)
  this->active_index_ = index;
  ESP_LOGD(TAG, "Sending state %s (index %zi)", this->traits.get_options()[index],
           index);
#if defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_select_update(this);
#endif
#else
  this->state = this->traits.get_options()[index];
  ESP_LOGD(TAG, "Sending state %s (index %zi)", this->state.c_str(), index);
#endif
  this->state_callback_.call(index);
}

void Select::publish_unknown_() {
  this->set_has_state(false);
  ESP_LOGD(TAG, "Sending state 'unknown' (index %zi)", static_cast<size_t>(-1));
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 11, 0)
  this->active_index_ = static_cast<size_t>(-1);
#if defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_select_update(this);
#endif
#else
  this->state = "unknown";
#endif
  this->state_callback_.call(static_cast<size_t>(-1));
}
}  // namespace m3_vedirect
}  // namespace esphome
