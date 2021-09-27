#pragma once

template <typename Traits>
class scoped_generic : public Traits {
public:
    using element_type = typename T::value_type;
    using traits_type = Traits;

    scoped_generic() : data(traits_type::invalid_value()) {}
    scoped_generic(const element_type& value) : data(value) {}
    scoped_generic(scoped_generic<Traits>&& value) : data(value.release()) {}
    scoped_generic(const scoped_generic&) = delete;
    scoped_generic& operator=(const scoped_generic&) = delete;

    virtual ~scoped_generic() {
        free_if_necessary();
    }

    scoped_generic& operator=(scoped_generic<Traits>&& value) {
        reset(value.release());
        return *this;
    }

    void reset(const element_type& value = traits_type::invalid_value()) {
        if (data == value) return;
        free_if_necessary();
        data = value;
    }

    void swap(scoped_generic& other) {
        if (&other == this) return;

        using std::swap;
        swap(data, other.data);
    }

    element_type release() [[nodiscard]] {
        element_type old_data = std::move(data);
        data = traits_type::invalid_value();
        return old_data;
    }

    const element_type& get() const { return data; }
    bool is_valid() const { return data != traits_type::invalid_value(); }
    bool operator==(const element_type& value) const { return data == value; }
    bool operator!=(const element_type& value) const { return data != value; }

    operator element_type() const { return data; }

private:
    void free_if_necessary() {
        if (data != traits_type::invalid_value()) {
            traits_type::free(data);
            data = traits_type::invalid_value();
        }
    }

    T data;
};

template <typename Traits>
void swap(scoped_generic<Traits>& a, scoped_generic<Traits>& b) {
    a.swap(b);
}

template <typename Traits>
bool operator==(const typename Traits::value_type& value, const scoped_generic<Traits>& scoped) {
    return value == scoped.get();
}

template <typename Traits>
bool operator!=(const typename Traits::value_type& value, const scoped_generic<Traits>& scoped) {
    return value != scoped.get();
}
