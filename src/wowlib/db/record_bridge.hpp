#pragma once

/** @file
    The one small per-record adapter: TypedRecordSink<Record> and
    TypedRecordSource<Record> implement the non-templated RecordSink /
    RecordSource (codec.hpp) over a std::vector<Record>. Reflection maps a
    runtime (column, element) to the column-th record member — schema columns are
    one-per-member in declaration order — and moves one field value across, which
    is the ONLY code the codecs need instantiated per generated table. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <meta>

#include <wowlib/db/codec.hpp>
#include <wowlib/db/schema.hpp>

namespace wowlib::db::detail
{
  /** Whether member type @a M is a scalar/array integer (bool excluded). */
  template <typename M>
  concept int_member = (std::integral<typename element_traits<M>::element>
                        && !std::same_as<typename element_traits<M>::element, bool>);

  // --- one-field moves keyed on the member type (scalar, array, LocString) ---

  template <typename M>
  void member_set_int(M& member, std::size_t elem, std::int64_t v)
  {
    using E = typename element_traits<M>::element;
    if constexpr (locstring_traits<M>::value)
      member.flags = static_cast<std::uint32_t>(v);  // elem == locale_count
    else if constexpr (int_member<M> && element_traits<M>::is_array)
      member[elem] = static_cast<E>(v);
    else if constexpr (int_member<M>)
      member = static_cast<E>(v);
    // float / string members never receive set_int.
  }

  template <typename M>
  void member_set_float(M& member, std::size_t elem, float v)
  {
    using E = typename element_traits<M>::element;
    if constexpr (std::same_as<E, float> && element_traits<M>::is_array)
      member[elem] = v;
    else if constexpr (std::same_as<E, float>)
      member = v;
  }

  template <typename M>
  void member_set_string(M& member, std::size_t elem, std::string_view v)
  {
    using E = typename element_traits<M>::element;
    if constexpr (locstring_traits<M>::value)
      member.values[elem] = std::string{v};
    else if constexpr (std::same_as<E, std::string> && element_traits<M>::is_array)
      member[elem] = std::string{v};
    else if constexpr (std::same_as<E, std::string>)
      member = std::string{v};
  }

  template <typename M>
  std::int64_t member_get_int(const M& member, std::size_t elem)
  {
    if constexpr (locstring_traits<M>::value)
      return static_cast<std::int64_t>(member.flags);  // elem == locale_count
    else if constexpr (int_member<M> && element_traits<M>::is_array)
      return static_cast<std::int64_t>(member[elem]);
    else if constexpr (int_member<M>)
      return static_cast<std::int64_t>(member);
    else
      return 0;
  }

  template <typename M>
  std::uint32_t member_get_slot(const M& member, std::size_t elem)
  {
    using E = typename element_traits<M>::element;
    if constexpr (element_traits<M>::is_array)
    {
      if constexpr (std::same_as<E, float>)
        return std::bit_cast<std::uint32_t>(member[elem]);
      else if constexpr (int_member<M>)
        return static_cast<std::uint32_t>(member[elem]);
      else
        return 0;
    }
    else if constexpr (std::same_as<E, float>)
      return std::bit_cast<std::uint32_t>(member);
    else if constexpr (int_member<M>)
      return static_cast<std::uint32_t>(member);
    else
      return 0;
  }

  template <typename M>
  std::string_view member_get_string(const M& member, std::size_t elem)
  {
    using E = typename element_traits<M>::element;
    if constexpr (locstring_traits<M>::value)
      return member.values[elem];
    else if constexpr (std::same_as<E, std::string> && element_traits<M>::is_array)
      return member[elem];
    else if constexpr (std::same_as<E, std::string>)
      return member;
    else
      return {};
  }

  template <typename Record>
  std::uint32_t record_id(const Record& record)
  {
    static constexpr auto members = record_members<Record>();
    std::uint32_t id = 0;
    template for (constexpr auto m : members)
      if constexpr (annotation<id_spec, m>().has_value())
        id = static_cast<std::uint32_t>(record.[:m:]);
    return id;
  }

  template <typename Record>
  void set_record_id(Record& record, std::uint32_t id)
  {
    static constexpr auto members = record_members<Record>();
    template for (constexpr auto m : members)
      if constexpr (annotation<id_spec, m>().has_value())
        record.[:m:] = static_cast<std::remove_cvref_t<decltype(record.[:m:])>>(id);
  }
}

namespace wowlib::db
{
  /** RecordSink over a live std::vector<Record>& — the decode target. */
  template <typename Record>
  class TypedRecordSink final : public RecordSink
  {
  public:
    explicit TypedRecordSink(std::vector<Record>& records) : records_{records} {}

    void clear() override { records_.clear(); }
    void reserve(std::size_t n) override { records_.reserve(n); }
    std::size_t add() override { records_.emplace_back(); return records_.size() - 1; }
    std::size_t size() const override { return records_.size(); }
    std::uint32_t id_of(std::size_t record) const override
    {
      return detail::record_id(records_[record]);
    }

    void set_int(std::size_t record, std::size_t column, std::size_t element,
                 std::int64_t value) override
    {
      dispatch(record, column, [&](auto& member) {
        detail::member_set_int(member, element, value);
      });
    }
    void set_float(std::size_t record, std::size_t column, std::size_t element,
                   float value) override
    {
      dispatch(record, column, [&](auto& member) {
        detail::member_set_float(member, element, value);
      });
    }
    void set_string(std::size_t record, std::size_t column, std::size_t element,
                    std::string_view value) override
    {
      dispatch(record, column, [&](auto& member) {
        detail::member_set_string(member, element, value);
      });
    }

    void clone_with_id(std::size_t src, std::uint32_t new_id) override
    {
      Record clone = records_[src];
      detail::set_record_id(clone, new_id);
      records_.push_back(std::move(clone));
    }
    std::size_t find_by_id(std::uint32_t id) const override
    {
      for (std::size_t i = 0; i < records_.size(); ++i)
        if (detail::record_id(records_[i]) == id)
          return i;
      return records_.size();
    }

  private:
    /** Invoke @a fn on the @a column-th member of records_[record]. */
    template <typename Fn>
    void dispatch(std::size_t record, std::size_t column, Fn&& fn)
    {
      static constexpr auto members = detail::record_members<Record>();
      std::size_t i = 0;
      Record& r = records_[record];
      template for (constexpr auto m : members)
      {
        if (i == column) { fn(r.[:m:]); return; }
        ++i;
      }
    }

    std::vector<Record>& records_;
  };

  /** RecordSource over a const std::vector<Record>& — the encode source. */
  template <typename Record>
  class TypedRecordSource final : public RecordSource
  {
  public:
    explicit TypedRecordSource(const std::vector<Record>& records) : records_{records} {}

    std::size_t size() const override { return records_.size(); }
    std::uint32_t id_of(std::size_t record) const override
    {
      return detail::record_id(records_[record]);
    }

    std::int64_t get_int(std::size_t record, std::size_t column,
                         std::size_t element) const override
    {
      std::int64_t out = 0;
      dispatch(record, column, [&](const auto& member) {
        out = detail::member_get_int(member, element);
      });
      return out;
    }
    std::uint32_t get_slot(std::size_t record, std::size_t column,
                           std::size_t element) const override
    {
      std::uint32_t out = 0;
      dispatch(record, column, [&](const auto& member) {
        out = detail::member_get_slot(member, element);
      });
      return out;
    }
    std::string_view get_string(std::size_t record, std::size_t column,
                                std::size_t element) const override
    {
      std::string_view out;
      dispatch(record, column, [&](const auto& member) {
        out = detail::member_get_string(member, element);
      });
      return out;
    }

  private:
    template <typename Fn>
    void dispatch(std::size_t record, std::size_t column, Fn&& fn) const
    {
      static constexpr auto members = detail::record_members<Record>();
      std::size_t i = 0;
      const Record& r = records_[record];
      template for (constexpr auto m : members)
      {
        if (i == column) { fn(r.[:m:]); return; }
        ++i;
      }
    }

    const std::vector<Record>& records_;
  };
}
