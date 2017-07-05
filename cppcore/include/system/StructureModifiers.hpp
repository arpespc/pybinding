#pragma once
#include "Lattice.hpp"

#include "numeric/dense.hpp"

#include <vector>
#include <algorithm>
#include <memory>

namespace cpb {

class Foundation;
struct System;

/**
 Modify the state (valid or invalid) of lattice sites, e.g. to create vacancies
 */
class SiteStateModifier {
public:
    using Function = std::function<void(Eigen::Ref<ArrayX<bool>> state, CartesianArrayConstRef pos,
                                        string_view sublattice)>;
    Function apply; ///< to be user-implemented
    int min_neighbors; ///< afterwards, remove sites with less than this number of neighbors

    SiteStateModifier(Function const& apply, int min_neighbors = 0)
        : apply(apply), min_neighbors(min_neighbors) {}
};

/**
 Modify the position of lattice sites, e.g. to apply geometric deformations
 */
class PositionModifier {
public:
    using Function = std::function<void(CartesianArrayRef position, string_view sublattice)>;
    Function apply; ///< to be user-implemented

    PositionModifier(Function const& apply) : apply(apply) {}
};

/**
 Helper class for passing sublattice information
 */
struct SubIdRef {
    ArrayX<storage_idx_t> const& ids;
    std::unordered_map<std::string, storage_idx_t> name_map;
};

/**
 Introduces a new hopping family (with new hop_id) via a list of index pairs

 This can be used to create new hoppings independent of the main Lattice definition.
 It's especially useful for creating additional local hoppings, e.g. to model defects.
 */
class HoppingGenerator {
public:
    /// Site index pairs which should form new hoppings
    struct Result {
        ArrayXi from;
        ArrayXi to;
    };
    using Function = std::function<Result(cpb::CartesianArray const&, SubIdRef)>;

    std::string name; ///< friendly hopping identifier - will be added to lattice registry
    std::complex<double> energy; ///< hopping energy - also added to lattice registry
    Function make; ///< function which will generate the new hopping index pairs

    HoppingGenerator(std::string const& name, std::complex<double> energy, Function const& make)
        : name(name), energy(energy), make(make) {}

    explicit operator bool() const { return static_cast<bool>(make); }
};

template<class M> void apply(M const&, Foundation&) {}
void apply(SiteStateModifier const& m, Foundation& f);
void apply(PositionModifier const& m, Foundation& f);

template<class M> void apply(M const&, System&) {}
void apply(HoppingGenerator const& g, System& s);

template<class M> constexpr bool requires_system(M const&) { return false; }
constexpr bool requires_system(HoppingGenerator const&) { return true; }

template<class M> constexpr bool is_generator(M const&) { return false; }
constexpr bool is_generator(HoppingGenerator const&) { return true; }

/**
 Polymorphic storage for system/foundation modifiers

 Behaves like a common base for several classes but without actually needing
 to inherit from anything -- a class just needs to satisfy the interface.
 This allows us to use value semantics with polymorphic behavior.

 See: "Inheritance Is The Base Class of Evil" by Sean Parent.
 */
class StructureModifier {
public:
    template<class T>
    StructureModifier(T x) : impl(std::make_shared<Storage<T>>(std::move(x))) { }

    friend void apply(StructureModifier const& x, Foundation& f) { x.impl->v_apply(f); }
    friend void apply(StructureModifier const& x, System& s) { x.impl->v_apply(s); }
    friend bool requires_system(StructureModifier const& x) { return x.impl->v_requires_system(); }
    friend bool is_generator(StructureModifier const& x) { return x.impl->v_is_generator(); }

private:
    struct Interface {
        Interface() = default;
        virtual ~Interface() = default;

        Interface(Interface const&) = delete;
        Interface(Interface&&) = delete;
        Interface& operator=(Interface const&) = delete;
        Interface& operator=(Interface&&) = delete;

        virtual void v_apply(Foundation&) const = 0;
        virtual void v_apply(System&) const = 0;
        virtual bool v_requires_system() const = 0;
        virtual bool v_is_generator() const = 0;
    };

    template<class T>
    struct Storage : Interface {
        Storage(T x) : data(std::move(x)) { }

        void v_apply(Foundation& f) const override { apply(data, f); }
        void v_apply(System& s) const override { apply(data, s); }
        bool v_requires_system() const override { return requires_system(data); }
        bool v_is_generator() const override { return is_generator(data); }

        T data;
    };

    std::shared_ptr<Interface const> impl;
};

} // namespace cpb
