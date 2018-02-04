#include <win32/time.h>
#include <math/geometry.h>

#include <Windows.h>

namespace win32 {

static LARGE_INTEGER p_perf_freq = { 0 };
static LARGE_INTEGER p_perf_counter = { 0 };

void Timers::init()
{
  QueryPerformanceFrequency(&p_perf_freq);

  tick();
}

void Timers::finalize()
{
}

void Timers::tick()
{
  QueryPerformanceCounter(&p_perf_counter);
}

Time Timers::ticks()
{
  return (Time)p_perf_counter.QuadPart;
}

Time Timers::time_s()
{
  return ticks_to_s(ticks());
}

Time Timers::time_ms()
{
  return ticks_to_ms(ticks());
}

Time Timers::time_us()
{
  return ticks_to_us(ticks());
}

Time Timers::ticks_to_s(Time ticks)
{
  return (Time)(ticks / p_perf_freq.QuadPart);
}

Time Timers::ticks_to_ms(Time ticks)
{
  return (Time)((ticks*1000ull) / p_perf_freq.QuadPart);
}

Time Timers::ticks_to_us(Time ticks)
{
  return (Time)((ticks*1000000ull)/ p_perf_freq.QuadPart);
}

Time Timers::s_to_ticks(Time secs)
{
  return secs*p_perf_freq.QuadPart;
}

Time Timers::ms_to_ticks(Time msecs)
{
  return (msecs*p_perf_freq.QuadPart) / 1000ull;
}

Time Timers::us_to_ticks(Time usecs)
{
  return (usecs*p_perf_freq.QuadPart) / 1000000ull;
}

Timer::Timer() :
  m_started(~0u)
{
  reset();
}

void Timer::reset()
{
  m_started = Timers::ticks();
}

Time Timer::delta()
{
  return Timers::ticks() - m_started;
}

Time DeltaTimer::elapsedSeconds()
{
  return Timers::ticks_to_s(delta());
}

Time DeltaTimer::elapsedMillieconds()
{
  return Timers::ticks_to_ms(delta());
}

Time DeltaTimer::elapsedUseconds()
{
  return Timers::ticks_to_us(delta());
}

double DeltaTimer::elapsedSecondsf()
{
  auto dt = (double)Timers::ticks_to_us(delta());
  return dt/1000000.0;
}


DurationTimer::DurationTimer() :
  m_duration(~0u)
{
}

DurationTimer& DurationTimer::durationSeconds(Time duration)
{
  m_duration = Timers::s_to_ticks(duration);

  return *this;
}

DurationTimer& DurationTimer::durationMilliseconds(Time duration)
{
  m_duration = Timers::ms_to_ticks(duration);

  return *this;
}

DurationTimer& DurationTimer::durationUseconds(Time duration)
{
  m_duration = Timers::us_to_ticks(duration);

  return *this;
}

bool DurationTimer::elapsed()
{
  return m_duration != ~0u ? Timers::ticks() >= (m_started + m_duration)  : false; 
}

float DurationTimer::elapsedf()
{
  double x = (double)delta() / (double)m_duration;
  return clamp((float)x, 0.0f, 1.0f);
}

void DurationTimer::clear()
{
  m_duration = ~0u;
}

LoopTimer::LoopTimer() :
  m_loops(0)
{
}

LoopTimer& LoopTimer::durationSeconds(Time duration)
{
  DurationTimer::durationSeconds(duration);

  return *this;
}

LoopTimer & LoopTimer::durationMilliseconds(Time duration)
{
  DurationTimer::durationMilliseconds(duration);

  return *this;
}

LoopTimer & LoopTimer::durationUseconds(Time duration)
{
  DurationTimer::durationUseconds(duration);

  return *this;
}

u64 LoopTimer::loops()
{
  tick();
  return m_loops;
}

float LoopTimer::elapsedf()
{
  return tick();
}

double LoopTimer::tick()
{
  double loops = 0;
  double x = (double)delta() / (double)m_duration;

  x = modf(x, &loops);
  m_loops = loops;

  return x;
}

}