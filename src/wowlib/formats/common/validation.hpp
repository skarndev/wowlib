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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/core/error.hpp>

namespace wowlib::formats
{
  /** How a validation finding affects the file's fitness for the client. */
  enum class ValidationSeverity : std::uint8_t
  {
    warning,  /**< Suspicious, but real client files ship it; the file loads. */
    error     /**< The client would misread or crash on a file written like this. */
  };

  /** One validate() finding: where it is, how bad it is, what is wrong. */
  struct ValidationIssue
  {
    /** The finding's severity (see the file-level policy). */
    ValidationSeverity severity = ValidationSeverity::error;

    /** Member path from the validated entity, e.g. "groups[3].body.indices". */
    std::string path;

    /** What is wrong, with the numbers involved. */
    std::string message;

    bool operator==(const ValidationIssue&) const = default;
  };

  /** Everything one validate() pass found, in member order. A linter-style
      collection — validation never stops at the first finding, so a broken
      entity reports every violated contract at once. */
  class ValidationReport
  {
  public:
    /** Record a finding.
        @param severity the finding's severity.
        @param path     member path from the validated entity ("" for
                        entity-level findings; nesting walkers prefix it).
        @param message  what is wrong, with the numbers involved. */
    void add(ValidationSeverity severity, std::string path, std::string message)
    {
      issues_.push_back({severity, std::move(path), std::move(message)});
    }

    /** Record an error finding (see add()).
        @param path    member path from the validated entity.
        @param message what is wrong, with the numbers involved. */
    void add_error(std::string path, std::string message)
    {
      add(ValidationSeverity::error, std::move(path), std::move(message));
    }

    /** Record a warning finding (see add()).
        @param path    member path from the validated entity.
        @param message what is wrong, with the numbers involved. */
    void add_warning(std::string path, std::string message)
    {
      add(ValidationSeverity::warning, std::move(path), std::move(message));
    }

    /** @return whether the entity is fit to write: no error-severity findings
                (warnings do not fail a report). */
    [[nodiscard]] bool ok() const
    {
      return std::ranges::none_of(issues_, [](const ValidationIssue& issue) {
        return issue.severity == ValidationSeverity::error;
      });
    }

    /** @return the number of error-severity findings. */
    [[nodiscard]] std::size_t error_count() const
    {
      return static_cast<std::size_t>(
        std::ranges::count(issues_, ValidationSeverity::error, &ValidationIssue::severity));
    }

    /** @return the number of warning-severity findings. */
    [[nodiscard]] std::size_t warning_count() const
    {
      return issues_.size() - error_count();
    }

    /** @return every finding, in the order the walk recorded them. */
    [[nodiscard]] const std::vector<ValidationIssue>& issues() const { return issues_; }

    /** @return the total finding count (errors and warnings). */
    [[nodiscard]] std::size_t size() const { return issues_.size(); }

    /** Prefix the paths of every finding recorded since @a mark with
        @a prefix — how a nesting walk scopes a sub-entity's findings
        ("indices" becomes "groups[3].indices").
        @param mark   the size() observed before the sub-entity's walk.
        @param prefix the sub-entity's member path. */
    void prefix_from(std::size_t mark, std::string_view prefix)
    {
      for (std::size_t i = mark; i < issues_.size(); ++i)
      {
        ValidationIssue& issue = issues_[i];
        issue.path = issue.path.empty()
                       ? std::string{prefix}
                       : std::format("{}.{}", prefix, issue.path);
      }
    }

    /** Fold the report into a Result: the monadic face of validation, for
        `entity.ensure_valid().and_then(...)` chains.
        @return success when ok(); otherwise an InvalidEntityState error whose
                message lists the first findings (capped, with a remainder
                note). */
    [[nodiscard]] Result<void> to_result() const
    {
      if (ok())
        return {};
      constexpr std::size_t max_listed = 8;
      std::string message = std::format("entity fails validation with {} error(s), {} warning(s):",
                                        error_count(), warning_count());
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
  };
}
