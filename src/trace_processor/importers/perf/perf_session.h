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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PERF_PERF_SESSION_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PERF_PERF_SESSION_H_

#include <sys/types.h>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_event_attr.h"

namespace perfetto::trace_processor::perf_importer {

// Helper to deal with perf_event_attr instances in a perf file.
class PerfSession : public RefCounted {
 public:
  class Builder {
   public:
    explicit Builder(uint32_t perf_session_id)
        : perf_session_id_(perf_session_id) {}
    base::StatusOr<RefPtr<PerfSession>> Build();
    Builder& AddAttrAndIds(perf_event_attr attr, std::vector<uint64_t> ids) {
      attr_with_ids_.push_back({std::move(attr), std::move(ids)});
      return *this;
    }

   private:
    struct PerfEventAttrWithIds {
      perf_event_attr attr;
      std::vector<uint64_t> ids;
    };

    uint32_t perf_session_id_;
    std::vector<PerfEventAttrWithIds> attr_with_ids_;
  };

  uint32_t perf_session_id() const { return perf_session_id_; }

  RefPtr<const PerfEventAttr> FindAttrForEventId(uint64_t id) const;

  base::StatusOr<RefPtr<const PerfEventAttr>> FindAttrForRecord(
      const perf_event_header& header,
      const TraceBlobView& payload) const;

 private:
  PerfSession(uint32_t perf_session_id,
              base::FlatHashMap<uint64_t, RefPtr<PerfEventAttr>> attrs_by_id,
              bool has_single_perf_event_attr)
      : perf_session_id_(perf_session_id),
        attrs_by_id_(std::move(attrs_by_id)),
        has_single_perf_event_attr_(has_single_perf_event_attr) {}

  bool ReadEventId(const perf_event_header& header,
                   const TraceBlobView& payload,
                   uint64_t& id) const;

  uint32_t perf_session_id_;
  base::FlatHashMap<uint64_t, RefPtr<PerfEventAttr>> attrs_by_id_;
  // Multiple ids can map to the same perf_event_attr. This member tells us
  // whether there was only one perf_event_attr (with potentially different ids
  // associated). This makes the attr lookup given a record trivial and not
  // dependant no having any id field in the records.
  bool has_single_perf_event_attr_;
};

}  // namespace perfetto::trace_processor::perf_importer

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PERF_PERF_SESSION_H_
