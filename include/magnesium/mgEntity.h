// Copyright �2018 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in Magnesium.h

#ifndef __ENTITY_H__MG__
#define __ENTITY_H__MG__

#include "mgRefCounter.h"
#include "bss-util/Map.h"
#include "bss-util/CompactArray.h"
#include "bss-util/TRBTree.h"
#include "bss-util/CacheAlloc.h"
#include "bss-util/BlockAllocMT.h"

namespace magnesium {
  typedef unsigned short ComponentID;
  typedef int EventID;

  struct MG_DLLEXPORT MagnesiumAllocatorBase {
    static bss::CachePolicy<char> GeneralAlloc;
    void* allocate(size_t cnt, void* p = nullptr, size_t old = 0);
    void deallocate(void* p, size_t sz = 0);
  };

  template<typename T>
  struct BSS_COMPILER_DLLEXPORT MagnesiumAllocator : MagnesiumAllocatorBase {
    typedef T value_type;
    typedef void policy_type;
    template<class U> using rebind = MagnesiumAllocator<U>;
    MagnesiumAllocator() = default;
    template <class U> constexpr MagnesiumAllocator(const MagnesiumAllocator<U>&) noexcept {}

    inline T* allocate(size_t cnt, T* p = nullptr, size_t old = 0) noexcept { return reinterpret_cast<T*>(MagnesiumAllocatorBase::allocate(cnt * sizeof(T), p, old * sizeof(T))); }
    inline void deallocate(T* p, size_t sz = 0) noexcept { MagnesiumAllocatorBase::deallocate(p, sz * sizeof(T)); }
  };

#ifdef BSS_DEBUG
  template<class T> // This is a debug tracking class that ensures you never add or remove a component
  struct mgComponentRef // while you have an active reference to it, because the pointer gets invalidated
  {
    mgComponentRef(const mgComponentRef& r) : ref(r.ref) { ++Counter(); }
    mgComponentRef(T* r) : ref(r) { ++Counter(); }
    ~mgComponentRef() { --Counter(); }
    T* operator ->() { return ref; }
    const T* operator ->() const { return ref; }
    T& operator *() { return *ref; }
    bool operator !() const { return !ref; }
    const T& operator *() const { return *ref; }
    operator T*() { return ref; }

    static int& Counter() { static int count = 0; return count; }
    T* ref;
  };
#define COMPONENT_REF(T) mgComponentRef<T>
#else
#define COMPONENT_REF(T) T*
#endif
  
  struct MG_DLLEXPORT mgComponentCounter { protected: static ComponentID curID; static ComponentID curGraphID; };

  struct MG_DLLEXPORT mgEntity : public mgRefCounter, public bss::internal::TRB_NodeBase<mgEntity>
  {
    explicit mgEntity(mgEntity* parent = 0, int order = 0, ComponentID hint = 1);
    mgEntity(mgEntity&& mov);
    virtual ~mgEntity();
    void ComponentListInsert(ComponentID id, ComponentID graphid, size_t);
    void ComponentListRemove(ComponentID id, ComponentID graphid);
    size_t& ComponentListGet(ComponentID id);
    mgComponentCounter* GetByID(ComponentID id);
    template<class T> // Gets a component of type T if it belongs to this entity, otherwise returns NULL
    inline COMPONENT_REF(T::template TYPE) Get() 
    { 
      ComponentID index = _componentlist.Get(T::ID()); 
      return index == (ComponentID)~0 ? nullptr : T::Store().Get(_componentlist(index));
    }
    template<class T>
    inline COMPONENT_REF(const T::template TYPE) Get() const
    {
      ComponentID index = _componentlist.Get(T::ID());
      return index == (ComponentID)~0 ? nullptr : T::Store().Get(_componentlist(index));
    }
    template<class T> // Adds a component of type T to this entity if it doesn't already exist
    inline COMPONENT_REF(T::template TYPE) Add() 
    { 
      if(_componentlist.Get(T::ID()) == (ComponentID)~0)
        T::Store().Add<T>(this); 
      return Get<T::template TYPE>(); 
    }
    template<class T> // Removes a component of type T from this entity
    inline bool Remove() 
    { 
      ComponentID index = _componentlist.Get(T::ID()); 
      if(index != (ComponentID)~0)
      {
        T::RemoveImplied(this);
        return T::Store().Remove(_componentlist(index));
      }
      return false;
    }
    inline mgEntity* Parent() const { return _parent; }
    inline mgEntity* Children() const { return _first; }
    inline mgEntity* Next() const { return next; }
    void SetParent(mgEntity* parent);
    inline int Order() const { return _order; }
    void SetOrder(int order);
    const char* GetName() const { return _name; }
    void SetName(const char* name) { _name = name; }
    inline bss::Slice<const std::tuple<ComponentID, size_t>> GetComponents() const {
      return bss::Slice<const std::tuple<ComponentID, size_t>>(_componentlist.begin(), _componentlist.Length());
    }
    void Revalidate();

    size_t entityId;
    size_t childhint; // Bitfield of scenegraph components our children might have
    size_t graphcomponents;

    static inline char Comp(const mgEntity& l, const mgEntity& r) { char c = SGNCOMPARE(l._order, r._order); return !c ? SGNCOMPARE(&l, &r) : c; }
    static void* operator new(std::size_t sz);
    static void operator delete(void* ptr, std::size_t sz);

  protected:
    explicit mgEntity(bool isNIL);
    virtual void _addchild(mgEntity* child);
    virtual bool _checkchild(mgEntity* child);
    void _removechild(mgEntity* child);
    void _propagateIDs();
    void _registerEvent(EventID event, ComponentID id, void(*f)());
    void _registerHook(EventID event, void(mgEntity::*f)());
    template<typename R, typename... Args>
    inline R _sendEvent(EventID ID, Args... args)
    {
      if(void(mgEntity::*hook)() = _hooklist.Get(ID))
        (this->*reinterpret_cast<R(mgEntity::*)(Args...)>(hook))(args...);
      auto pair = _eventlist.GetValue(_eventlist.Iterator(ID));
      if(!pair)
        return R();
      assert(_componentlist.Get(pair->first) != (ComponentID)~0);
      size_t index = _componentlist.GetData(pair->first);
      mgComponentCounter* base = mgComponentStoreBase::GetComponent(pair->first, index);
      assert(base != 0);
      R(*f)(mgComponentCounter*, Args...) = reinterpret_cast<R(*)(mgComponentCounter*, Args...)>(pair->second);
      assert(f != 0);
      return f(base, args...);
    }
    
    mgEntity* _getRoot();

    bss::Map<ComponentID, size_t, bss::CompT<ComponentID>, ComponentID, bss::ARRAY_SIMPLE, MagnesiumAllocator<std::tuple<ComponentID, size_t>>> _componentlist; // You can't interact with componentlist directly because it violates DLL bounderies
    bss::HashIns<EventID, std::pair<ComponentID, void(*)()>, bss::ARRAY_SIMPLE, MagnesiumAllocator<char>> _eventlist;
    bss::HashIns<EventID, void(mgEntity::*)(), bss::ARRAY_SIMPLE, MagnesiumAllocator<char>> _hooklist;
    mgEntity* _parent;
    mgEntity* _first;
    mgEntity* _last;
    mgEntity* _children;
    int _order;
    const char* _name;

    friend class mgEngine;
    template<EventID ID, typename R, typename... Args>
    friend struct EventDef;
    static mgEntity NIL;
    static bss::LocklessBlockPolicy<mgEntity> EntityAlloc;
  };

  template<int I>
  struct Event;

  template<EventID ID, typename R, typename... Args>
  struct EventDef
  {
    BSS_FORCEINLINE static R Send(mgEntity* e, Args... args) { return e->_sendEvent<R, Args...>(ID, args...); }
    template<typename T>
    BSS_FORCEINLINE static void RegisterRaw(mgEntity* e, R(*f)(mgComponentCounter*, Args...)) { e->_registerEvent(ID, T::ID(), reinterpret_cast<void(*)()>(f)); }
    template<typename T, R(T::*F)(Args...)>
    BSS_FORCEINLINE static void Register(mgEntity* e) { e->_registerEvent(ID, T::ID(), reinterpret_cast<void(*)()>(&stub<T,F>)); }
    template<void(mgEntity::*F)(Args...)>
    BSS_FORCEINLINE static void Hook(mgEntity* e) { e->_registerHook(ID, reinterpret_cast<void(mgEntity::*)()>(F)); }
    template<typename T, typename... Inner>
    BSS_FORCEINLINE static auto Transfer(Inner... inner) { return T::F<ID, R, Args...>(inner...); }

  protected:
    template<typename T, R(T::*F)(Args...)>
    inline static R stub(mgComponentCounter* p, Args... args) { return (static_cast<T*>(p)->*F)(args...); }
  };

  enum EVENT_TYPE
  {
    EVENT_NONE = 0,
    EVENT_SETPOSITION,
    EVENT_SETPOSITION_INTERPOLATE,
    EVENT_SETROTATION,
  };

  template<> struct Event<EVENT_SETPOSITION> : EventDef<EVENT_SETPOSITION, void, float, float> {};
  template<> struct Event<EVENT_SETPOSITION_INTERPOLATE> : EventDef<EVENT_SETPOSITION_INTERPOLATE, void, float, float> {};
  template<> struct Event<EVENT_SETROTATION> : EventDef<EVENT_SETROTATION, void, float> {};
}

#endif