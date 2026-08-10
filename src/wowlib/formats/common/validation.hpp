#pragma once

/** @file
    The validation vocabulary: the severity scale, the single finding and the
    report validate() fills. Validation is a SEPARATE pass from write() — the
    serializer never runs it — asserting the logical integrity contracts a file
    must satisfy to load in the client (companion-chunk counts, index ranges,
    flag/presence coherence). Entities declare the machine-checkable contracts
    as annotations (see annotations.hpp); everything the annotations cannot
    express lives in per-entity validate_extra hooks. The walker driving both
    is detail::validate_entity (chunked_file.hpp).

    Severity policy: an ERROR means the client would misread (or crash on) a
    file written in this state; a WARNING marks a state real client files ship
    regardless — a freshly read, unmodified client file must validate with zero
    errors, and the integration corpus asserts exactly that. */

#include <meta>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/entity_reflect.hpp>

namespace wowlib::formats
{
  enum class [[
    =welder::weld,
    =welder::doc("How a validation finding affects the file's fitness for the "
                 "client.")
  ]] ValidationSeverity : std::uint8_t
  {
    warning [[=welder::doc("Suspicious, but real client files ship it; the file "
                           "loads.")]],
    error [[=welder::doc("The client would misread or crash on a file written "
                         "like this.")]]
  };

  struct [[
    =welder::weld,
    =welder::doc("One validate() finding: where it is, how bad it is, and what "
                 "is wrong.")
  ]] ValidationIssue
  {
    [[=welder::doc("Whether the client would misread the file (error) or merely "
                   "find it unusual (warning).")]]
    ValidationSeverity severity = ValidationSeverity::error;

    [[=welder::doc(R"(Member path from the validated entity, e.g.
                      "groups[3].body.indices".)")]]
    std::string path;

    [[=welder::doc("What is wrong, with the numbers involved.")]]
    std::string message;

    bool operator==(const ValidationIssue&) const = default;
  };

  class [[
    =welder::weld,
    =welder::doc(R"(
        Everything one validate() pass found, in member order. Validation is
        linter-style — it never stops at the first finding, so a broken entity
        reports every violated contract at once. A report is ok() when it holds
        no errors; warnings mark states real client files ship.)")
  ]] ValidationReport
  {
  public:
    /** The most findings one report holds. A walk over a corrupt file can
        violate a contract per element of a million-element array; past this
        many the report stops growing and records that it was truncated, so
        validating never becomes the memory problem. */
    static constexpr std::size_t max_findings = 1000;

    /** Record a finding, unless the report is already full().
        @param severity the finding's severity.
        @param path     member path from the validated entity ("" for
                        entity-level findings; nesting walkers prefix it).
        @param message  what is wrong, with the numbers involved. */
    [[=welder::mark::exclude]]
    void add(ValidationSeverity severity, std::string path, std::string message)
    {
      if (full())
      {
        truncated_ = true;
        return;
      }
      issues_.push_back({severity, std::move(path), std::move(message)});
    }

    /** @return whether the report has reached max_findings — walkers check
                this to abandon per-element loops early. */
    [[nodiscard]] [[=welder::mark::exclude]]
    bool full() const { return issues_.size() >= max_findings; }

    [[nodiscard]]
    [[=welder::getter,
      =welder::doc("Whether findings were dropped because the report hit its "
                   "size cap.")]]
    bool truncated() const { return truncated_; }

    /** Record an error finding (see add()).
        @param path    member path from the validated entity.
        @param message what is wrong, with the numbers involved. */
    [[=welder::mark::exclude]]
    void add_error(std::string path, std::string message)
    {
      add(ValidationSeverity::error, std::move(path), std::move(message));
    }

    /** Record a warning finding (see add()).
        @param path    member path from the validated entity.
        @param message what is wrong, with the numbers involved. */
    [[=welder::mark::exclude]]
    void add_warning(std::string path, std::string message)
    {
      add(ValidationSeverity::warning, std::move(path), std::move(message));
    }

    [[nodiscard]]
    [[=welder::getter,
      =welder::doc("Whether the entity is fit to write: no error-severity "
                   "findings (warnings do not fail a report).")]]
    bool ok() const
    {
      return std::ranges::none_of(issues_, [](const ValidationIssue& issue) {
        return issue.severity == ValidationSeverity::error;
      });
    }

    [[nodiscard]]
    [[=welder::getter, =welder::doc("The number of error-severity findings.")]]
    std::size_t error_count() const
    {
      return static_cast<std::size_t>(
        std::ranges::count(issues_, ValidationSeverity::error, &ValidationIssue::severity));
    }

    [[nodiscard]]
    [[=welder::getter, =welder::doc("The number of warning-severity findings.")]]
    std::size_t warning_count() const
    {
      return issues_.size() - error_count();
    }

    [[nodiscard]]
    [[=welder::getter,
      =welder::doc("Every finding, in the order the walk recorded them.")]]
    const std::vector<ValidationIssue>& issues() const { return issues_; }

    [[nodiscard]]
    [[=welder::getter,
      =welder::doc("The total finding count (errors and warnings).")]]
    std::size_t size() const { return issues_.size(); }

    /** Prefix the paths of every finding recorded since @a mark with
        @a prefix — how a nesting walk scopes a sub-entity's findings
        ("indices" becomes "groups[3].indices"). A path that already starts
        with a subscript joins without a separator, so an element walk under a
        member reads "bones[3].rotation" rather than "bones.[3].rotation".
        @param mark   the size() observed before the sub-entity's walk.
        @param prefix the sub-entity's member path. */
    [[=welder::mark::exclude]]
    void prefix_from(std::size_t mark, std::string_view prefix)
    {
      for (std::size_t i = mark; i < issues_.size(); ++i)
      {
        ValidationIssue& issue = issues_[i];
        issue.path = issue.path.empty()      ? std::string{prefix}
                     : issue.path.front() == '[' ? std::format("{}{}", prefix, issue.path)
                                                 : std::format("{}.{}", prefix, issue.path);
      }
    }

    /** Fold the report into a Result: the monadic face of validation, for
        `entity.ensure_valid().and_then(...)` chains.
        @return success when ok(); otherwise an InvalidEntityState error whose
                message lists the first findings (capped, with a remainder
                note). */
    [[nodiscard]] [[=welder::mark::exclude]]
    Result<void> to_result() const
    {
      if (ok())
        return {};
      constexpr std::size_t max_listed = 8;
      std::string message =
        std::format("entity fails validation with {} error(s), {} warning(s){}:", error_count(),
                    warning_count(), truncated_ ? " (report truncated)" : "");
      std::size_t listed = 0;
      for (const ValidationIssue& issue : issues_)
      {
        if (listed == max_listed)
        {
          message += std::format("\n  ... and {} more", issues_.size() - listed);
          break;
        }
        message += std::format("\n  {}: {}: {}",
                               issue.severity == ValidationSeverity::error ? "error" : "warning",
                               issue.path, issue.message);
        ++listed;
      }
      return make_error(ErrorCode::InvalidEntityState, std::move(message));
    }

    bool operator==(const ValidationReport&) const = default;

  private:
    std::vector<ValidationIssue> issues_;  /**< The findings, in walk order. */
    bool truncated_ = false;               /**< Whether add() dropped findings. */
  };

  /** An entity the walker can gate by client version: anything carrying the
      `static constexpr ClientVersion version` every format entity declares
      (chunked files, M2 offset blocks, ADT tiles and map chunks alike). */
  template <typename E>
  concept VersionedEntity = requires {
    { E::version } -> std::convertible_to<ClientVersion>;
  };

  namespace detail
  {
    /** A member the walker can iterate element-wise: std::vector, std::array,
        Repeated<> and std::string all qualify. */
    template <typename T>
    concept ValidatableSequence = requires(const T& value) {
      typename T::value_type;
      { value.size() } -> std::convertible_to<std::size_t>;
      value[std::size_t{0}];
    };

    /** A container of independent SLOTS rather than one logical array — a
        chunk that may appear several times, each occurrence complete in
        itself (Repeated<>, opting in with `validation_slots`). A count
        contract applies to every filled slot separately, where on a plain
        nested vector it would apply to the outer count. */
    template <typename T>
    concept SlotSequence = ValidatableSequence<T> && requires {
      { T::validation_slots } -> std::convertible_to<bool>;
    } && T::validation_slots;

    /** A type carrying the imperative half of its contracts (see the
        validate_extra hook). */
    template <typename T>
    concept HasValidateExtra = requires(const T& value, ValidationReport& report) {
      value.validate_extra(report);
    };

    /** Whether @a member carries any validation annotation. */
    consteval bool has_validation_annotation(std::meta::info member)
    {
      for (std::meta::info spec :
           {^^count_matches_spec, ^^count_multiple_of_spec, ^^count_exactly_spec, ^^indexes_spec,
            ^^indexes_optional_spec, ^^indexes_in_root_spec, ^^expected_value_spec,
            ^^nonempty_spec})
        if (!std::meta::annotations_of_with_type(member, spec).empty())
          return true;
      return false;
    }

    /** Whether validating a @a T can produce anything — it declares a
        validate_extra hook, carries an annotated member, or (recursively)
        holds something that does. Drives where the walk descends, so an entity
        pays no runtime cost for members with no contracts at all.

        Trivially-copyable types stop the recursion: binary structs carry no
        annotations by policy (their contracts live in the enclosing entity's
        hook), and scalars have none to carry. Standard-library class types
        stop it too — their internals are not our records — but sequences are
        followed into their element type first, so `std::vector<M2Bone>` is
        walked while `std::string` is not.
        @tparam T the member or record type to classify. */
    template <typename T>
    consteval bool has_validation_content()
    {
      if constexpr (HasValidateExtra<T>)
        return true;
      else if constexpr (std::is_trivially_copyable_v<T>)
        return false;
      else if constexpr (ValidatableSequence<T>)
        return has_validation_content<typename T::value_type>();
      else if constexpr (std::is_class_v<T> && !is_std_type<T>())
      {
        bool any = false;
        template for (constexpr auto m : members_of<T>())
        {
          if (has_validation_annotation(m))
            any = true;
          else if (has_validation_content<typename [:std::meta::type_of(m):]>())
            any = true;
        }
        return any;
      }
      else
        return false;
    }

    /** Report every element of @a values that is not a valid index into a
        @a target_count-element target, capped so a corrupt file cannot flood
        the report.
        @param values       the index elements to check.
        @param target_count the indexed member's element count.
        @param member       the checked member's path (for the findings).
        @param target       the indexed member's name (for the findings).
        @param report       the report findings land in. */
    template <typename Values>
    void validate_index_elements(const Values& values, std::size_t target_count,
                                 std::string_view member, std::string_view target,
                                 ValidationReport& report)
    {
      constexpr std::size_t max_reported = 8;
      std::size_t bad = 0;
      for (std::size_t i = 0; i < values.size(); ++i)
        if (static_cast<std::size_t>(values[i]) >= target_count)
          if (++bad <= max_reported)
            report.add_error(std::format("{}[{}]", member, i),
                             std::format("index {} out of range: {} holds {} element(s)",
                                         values[i], target, target_count));
      if (bad > max_reported)
        report.add_error(std::string{member},
                         std::format("... and {} more out-of-range indices into {}",
                                     bad - max_reported, target));
    }

    /** Whether @a value is the client's "no reference" sentinel for an index
        element: negative in a signed lookup, all-ones in an unsigned one (the
        0xFFFF an M2 lookup table leaves in unused slots).
        @param value the index element.
        @return whether it references nothing. */
    template <std::integral T>
    constexpr bool is_no_index(T value)
    {
      if constexpr (std::is_signed_v<T>)
        return value < 0;
      else
        return value == std::numeric_limits<T>::max();
    }

    /** Report every non-sentinel element of @a values that is not a valid
        index into a @a target_count-element target (see is_no_index and the
        indexes_optional annotation); capped like validate_index_elements.
        @param values       the index elements to check.
        @param target_count the indexed member's element count.
        @param member       the checked member's path (for the findings).
        @param target       the indexed member's name (for the findings).
        @param report       the report findings land in. */
    template <typename Values>
    void validate_optional_index_elements(const Values& values, std::size_t target_count,
                                          std::string_view member, std::string_view target,
                                          ValidationReport& report)
    {
      constexpr std::size_t max_reported = 8;
      std::size_t bad = 0;
      for (std::size_t i = 0; i < values.size(); ++i)
      {
        if (is_no_index(values[i]))
          continue;
        if (static_cast<std::size_t>(values[i]) >= target_count)
          if (++bad <= max_reported)
            report.add_error(std::format("{}[{}]", member, i),
                             std::format("index {} out of range: {} holds {} element(s)",
                                         values[i], target, target_count));
      }
      if (bad > max_reported)
        report.add_error(std::string{member},
                         std::format("... and {} more out-of-range indices into {}",
                                     bad - max_reported, target));
    }

    /** Apply one member's count_matches contract against its resolved sibling
        count (see the annotation): engaged (non-empty) values only.
        @param count        the member's element count.
        @param scale        the annotation's scale factor.
        @param target_count the sibling's element count.
        @param member       the checked member's path (for the findings).
        @param target       the sibling's name (for the findings).
        @param report       the report findings land in. */
    inline void validate_count_matches(std::size_t count, std::uint32_t scale,
                                       std::size_t target_count, std::string_view member,
                                       std::string_view target, ValidationReport& report)
    {
      if (count == 0 || count * scale == target_count)
        return;
      report.add_error(std::string{member},
                       scale == 1
                         ? std::format("count {} != {} count {}", count, target, target_count)
                         : std::format("count {} x {} != {} count {}", count, scale, target,
                                       target_count));
    }

    template <ClientVersion V, typename T>
    void validate_value(const T& value, ValidationReport& report);

    /** Walk @a value's annotation-declared contracts, gating members by client
        version @a V and descending into everything with validation content.
        The caller adds the validate_extra hook (see validate_value).
        @tparam V     the governing client version — the enclosing entity's, so
                      records inherit the version gating of the file they sit in.
        @param  value the entity or record to check.
        @param  report the report findings land in. */
    template <ClientVersion V, typename T>
    void validate_members(const T& value, ValidationReport& report)
    {
      static constexpr auto members = members_of<T>();
      template for (constexpr auto m : members)
      {
        if constexpr (version_active<V, m>())
        {
          constexpr const char* ident = std::define_static_string(std::meta::identifier_of(m));
          using M = [:std::meta::type_of(m):];
          const M& member = value.[:m:];

          if constexpr (constexpr auto ev = annotation<expected_value_spec, m>(); ev.has_value())
          {
            static_assert(std::is_integral_v<M>, "expected_value applies to integral members");
            if (static_cast<std::uint64_t>(member) != ev->value)
              report.add_error(ident, std::format("value {} != required {}", member, ev->value));
          }

          if constexpr (annotation<nonempty_spec, m>().has_value())
          {
            static_assert(requires { member.empty(); },
                          "nonempty applies to members with observable emptiness");
            if (member.empty())
              report.add_error(ident, "must not be empty");
          }

          if constexpr (constexpr auto ce = annotation<count_exactly_spec, m>(); ce.has_value())
          {
            static_assert(ValidatableSequence<M>, "count_exactly applies to sequence members");
            if (!member.empty() && member.size() != ce->count)
              report.add_error(ident, std::format("count {} != required {}", member.size(),
                                                  ce->count));
          }

          if constexpr (constexpr auto cmo = annotation<count_multiple_of_spec, m>();
                        cmo.has_value())
          {
            static_assert(ValidatableSequence<M>, "count_multiple_of applies to sequence members");
            if (!member.empty() && member.size() % cmo->divisor != 0)
              report.add_error(ident, std::format("count {} is not a multiple of {}",
                                                  member.size(), cmo->divisor));
          }

          if constexpr (constexpr auto cm = annotation<count_matches_spec, m>(); cm.has_value())
          {
            constexpr auto sibling = member_named<T>(cm->view());
            static_assert(sibling != std::meta::info{},
                          "count_matches names no member of this entity");
            using S = [:std::meta::type_of(sibling):];
            static_assert(ValidatableSequence<S>, "count_matches sibling must be a sequence");
            static_assert(ValidatableSequence<M>, "count_matches applies to sequence members");
            const S& target = value.[:sibling:];
            if constexpr (SlotSequence<M>)
            {
              // each occurrence is a complete array of its own: the MOTV
              // texcoord sets each hold one entry per MOVT vertex
              for (std::size_t slot = 0; slot < member.size(); ++slot)
                validate_count_matches(member[slot].size(), cm->scale, target.size(),
                                       std::format("{}[{}]", ident, slot), cm->view(), report);
            }
            else
            {
              validate_count_matches(member.size(), cm->scale, target.size(), ident, cm->view(),
                                     report);
              // Parallel nested arrays pair element-wise too (a WotLK+ M2 track
              // holds one timestamp array and one value array per sequence).
              // Only a std::vector element counts as nesting — a fixed-extent
              // element (a per-vertex std::array of bone indices) is one VALUE,
              // however sequence-shaped it looks.
              if constexpr (is_vector_v<typename M::value_type>
                            && is_vector_v<typename S::value_type>)
                for (std::size_t i = 0; i < member.size() && i < target.size(); ++i)
                  if (member[i].size() != target[i].size())
                    report.add_error(std::format("{}[{}]", ident, i),
                                     std::format("count {} != {}[{}] count {}", member[i].size(),
                                                 cm->view(), i, target[i].size()));
            }
          }

          if constexpr (constexpr auto ix = annotation<indexes_spec, m>(); ix.has_value())
          {
            static_assert(is_vector_v<M> && std::is_integral_v<typename M::value_type>,
                          "indexes applies to integral vector members");
            constexpr auto sibling = member_named<T>(ix->view());
            static_assert(sibling != std::meta::info{}, "indexes names no member of this entity");
            using S = [:std::meta::type_of(sibling):];
            static_assert(ValidatableSequence<S>, "indexes sibling must be a sequence");
            validate_index_elements(member, value.[:sibling:].size(), ident, ix->view(), report);
          }

          if constexpr (constexpr auto ixo = annotation<indexes_optional_spec, m>();
                        ixo.has_value())
          {
            static_assert(is_vector_v<M> && std::is_integral_v<typename M::value_type>,
                          "indexes_optional applies to integral vector members");
            constexpr auto sibling = member_named<T>(ixo->view());
            static_assert(sibling != std::meta::info{},
                          "indexes_optional names no member of this entity");
            using S = [:std::meta::type_of(sibling):];
            static_assert(ValidatableSequence<S>, "indexes_optional sibling must be a sequence");
            validate_optional_index_elements(member, value.[:sibling:].size(), ident, ixo->view(),
                                             report);
          }

          // descend: nested entities, records, and sequences of either
          if constexpr (has_validation_content<M>())
          {
            if constexpr (ValidatableSequence<M> && !HasValidateExtra<M>)
            {
              for (std::size_t i = 0; i < member.size() && !report.full(); ++i)
              {
                const std::size_t mark = report.size();
                validate_value<V>(member[i], report);
                report.prefix_from(mark, std::format("{}[{}]", ident, i));
              }
            }
            else
            {
              const std::size_t mark = report.size();
              validate_value<V>(member, report);
              report.prefix_from(mark, ident);
            }
          }
        }
      }
    }

    /** Validate one value of any shape: a versioned entity (gated by its OWN
        version), a plain record (gated by the enclosing @a V), or a sequence of
        either (walked element-wise, paths subscripted). Types with no
        validation content cost nothing — the walk is compiled out.
        @tparam V      the enclosing entity's client version.
        @param  value  the value to check.
        @param  report the report findings land in. */
    template <ClientVersion V, typename T>
    void validate_value(const T& value, ValidationReport& report)
    {
      if constexpr (!has_validation_content<T>())
        return;
      else if constexpr (ValidatableSequence<T> && !HasValidateExtra<T>)
      {
        for (std::size_t i = 0; i < value.size() && !report.full(); ++i)
        {
          const std::size_t mark = report.size();
          validate_value<V>(value[i], report);
          report.prefix_from(mark, std::format("[{}]", i));
        }
      }
      else
      {
        if constexpr (VersionedEntity<T>)
          validate_members<T::version>(value, report);
        else
          validate_members<V>(value, report);
        // the imperative complement: record-interior and flag/presence
        // contracts the annotations cannot express
        if constexpr (HasValidateExtra<T>)
          value.validate_extra(report);
      }
    }

    /** Validate a whole entity — the engine behind every `validate()` method;
        see ChunkedFile::validate() for the contract.
        @param entity the entity to check.
        @param report the report findings land in. */
    template <VersionedEntity E>
    void validate_entity(const E& entity, ValidationReport& report)
    {
      validate_value<E::version>(entity, report);
    }
  }
}
