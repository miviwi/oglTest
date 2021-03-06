#pragma once

#include <common.h>

#include <memory>

namespace os {

using Time = u64;

struct Input {
  static void deleter(Input *ptr);

  using Ptr = std::unique_ptr<Input, decltype(&Input::deleter)>;
  using Tag = const char *;

  static Ptr invalid_ptr();

  // Use example:
  //   if(auto mouse = input.get<Mouse>()) { ... }
  template <typename T>
  T *get()
  {
    return getTag() == T::tag() ? (T *)this : nullptr;
  }

  Time timestamp;

  virtual const char *dbg_TypeStr() const = 0;

protected:
  virtual Tag getTag() const = 0;
};

struct Mouse final : public Input {
  enum Button : u16 {
    Left   = 1<<0,
    Right  = 1<<1,
    Middle = 1<<2,
    X1     = 1<<3,
    X2     = 1<<4,

    DoubleClick = 1<<15,
  };

  enum Event : u16 {
    Move, Up, Down, Wheel,
  };

  u16 event;

  // Stores ALL of the held buttons at the time of the input
  u16 buttons;
  // Stores the Button directly related to the event, i.e.
  //   - When event == Mouse::Down, ev_data stores the pressed button
  //   - When event == Mouse::Up, ev_data stores the released button
  //
  // HOWEVER in the case of a Mouse::Wheel input the scroll value
  //   is stored
  int ev_data;

  float dx, dy;

  static Tag tag() { return "mouse"; }

  static Input::Ptr create();

  bool buttonDown(Button btn) const;
  bool buttonUp(Button btn) const;

  virtual const char *dbg_TypeStr() const final;

protected:
  virtual Tag getTag() const { return tag(); }
};

struct Keyboard final : public Input {
  enum Event : u16 {
    Invalid,
    KeyUp, KeyDown,
    SysUp, SysDown,
  };

  enum Modifier : u16 {
    Ctrl  = 1<<0,
    Shift = 1<<1,
    Alt   = 1<<2,
    Super = 1<<3,

    CapsLock = 1<<15,
  };

  enum Key {
    SpecialKey = (1<<16),

    Escape,
    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12,

    Up, Left, Down, Right,
    
    Tab, Enter, Backspace,

    Insert, Home, PageUp,
    Delete, End,  PageDown,

    NumLock, Print, ScrollLock, Pause,
  };

  u16 event;
  u16 modifiers;

  Time time_held;

  unsigned key; // raw scancode
  unsigned sym; // printed character

  static Tag tag() { return "kb"; }

  static Input::Ptr create();

  bool keyDown(unsigned k) const;
  bool keyUp(unsigned k) const;

  bool modifier(unsigned mod) const;

  bool special() const;

  virtual const char *dbg_TypeStr() const final;

protected:
  virtual Tag getTag() const { return tag(); }
};

}
