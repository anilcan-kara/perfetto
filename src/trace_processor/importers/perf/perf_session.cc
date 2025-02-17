/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/importers/perf/perf_session.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_event_attr.h"
#include "src/trace_processor/importers/perf/reader.h"

namespace perfetto::trace_processor::perf_importer {
namespace {
bool OffsetsMatch(const PerfEventAttr& attr, const PerfEventAttr& other) {
  return attr.id_offset_from_start() == other.id_offset_from_start() &&
         (!attr.sample_id_all() ||
          attr.id_offset_from_end() == other.id_offset_from_end());
}
}  // namespace

base::StatusOr<RefPtr<PerfSession>> PerfSession::Builder::Build() {
  if (attr_with_ids_.empty()) {
    return base::ErrStatus("No perf_event_attr");
  }

  const PerfEventAttr base_attr(attr_with_ids_[0].attr);

  base::FlatHashMap<uint64_t, RefPtr<PerfEventAttr>> attrs_by_id;
  for (const auto& entry : attr_with_ids_) {
    RefPtr<PerfEventAttr> attr(new PerfEventAttr(entry.attr));
    if (base_attr.sample_id_all() != attr->sample_id_all()) {
      return base::ErrStatus(
          "perf_event_attr with different sample_id_all values");
    }

    if (!OffsetsMatch(base_attr, *attr)) {
      return base::ErrStatus("perf_event_attr with different id offsets");
    }

    for (uint64_t id : entry.ids) {
      if (!attrs_by_id.Insert(id, attr).second) {
        return base::ErrStatus(
            "Same id maps to multiple perf_event_attr: %" PRIu64, id);
      }
    }
  }

  if (attr_with_ids_.size() > 1 &&
      (!base_attr.id_offset_from_start().has_value() ||
       (base_attr.sample_id_all() &&
        !base_attr.id_offset_from_end().has_value()))) {
    return base::ErrStatus("No id offsets for multiple perf_event_attr");
  }

  return RefPtr<PerfSession>(new PerfSession(
      perf_session_id_, std::move(attrs_by_id), attr_with_ids_.size() == 1));
}

base::StatusOr<RefPtr<const PerfEventAttr>> PerfSession::FindAttrForRecord(
    const perf_event_header& header,
    const TraceBlobView& payload) const {
  RefPtr<const PerfEventAttr> first(attrs_by_id_.GetIterator().value().get());
  if (has_single_perf_event_attr_) {
    return first;
  }

  if (header.type >= PERF_RECORD_USER_TYPE_START ||
      (header.type != PERF_RECORD_SAMPLE && !first->sample_id_all())) {
    return RefPtr<const PerfEventAttr>();
  }

  uint64_t id;
  if (!ReadEventId(header, payload, id)) {
    return base::ErrStatus("Failed to read record id");
  }

  auto it = FindAttrForEventId(id);
  if (!it) {
    return base::ErrStatus("No perf_event_attr for id %" PRIu64, id);
  }

  return it;
}

bool PerfSession::ReadEventId(const perf_event_header& header,
                              const TraceBlobView& payload,
                              uint64_t& id) const {
  const PerfEventAttr& first = *attrs_by_id_.GetIterator().value();
  Reader reader(payload.copy());

  if (header.type != PERF_RECORD_SAMPLE) {
    PERFETTO_CHECK(first.id_offset_from_end().has_value());
    if (reader.size_left() < *first.id_offset_from_end()) {
      return false;
    }
    const size_t off = reader.size_left() - *first.id_offset_from_end();
    return reader.Skip(off) && reader.Read(id);
  }

  PERFETTO_CHECK(first.id_offset_from_start().has_value());

  return reader.Skip(*first.id_offset_from_start()) && reader.Read(id);
}

RefPtr<const PerfEventAttr> PerfSession::FindAttrForEventId(uint64_t id) const {
  auto it = attrs_by_id_.Find(id);
  if (!it) {
    return RefPtr<const PerfEventAttr>();
  }
  return RefPtr<const PerfEventAttr>(it->get());
}

}  // namespace perfetto::trace_processor::perf_importer
