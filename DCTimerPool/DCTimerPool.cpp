#include "DCTimerPool.h"

TimerPool::TimerPool()
	: m_run(true)
	, m_thread{ [this]() { run(); } }
{

}

TimerPool::~TimerPool()
{
	stop();

	if (m_thread.joinable())
		m_thread.join();
}

void TimerPool::wake()
{
	std::lock_guard<decltype(m_mutex)> lock(m_mutex);
	m_cond.notify_all();
}

TimerPool::TimerHandle TimerPool::createTimer()
{
	std::lock_guard<decltype(m_mutex)> lock(m_mutex);

	auto newTimer = std::make_shared<Timer>(*this);
	m_timers.emplace_front(newTimer);

	return newTimer;
}

void TimerPool::deleteTimer(TimerHandle handle)
{
	std::lock_guard<decltype(m_mutex)> lock(m_mutex);

	if (auto timer = handle.lock())
	{
		m_timers.remove(timer);
		m_cond.notify_all();
	}
}

void TimerPool::run()
{
	std::unique_lock<decltype(m_mutex)> lock(m_mutex);

	while (m_run)
	{
		auto nowTime  = Clock::now();
		auto wakeTime = Clock::time_point::max();

		for (auto& t : m_timers)
		{
			const auto expiryTime = t->nextExpiry();

			if (nowTime >= expiryTime)
				wakeTime = t->fire();
			else if (expiryTime < wakeTime)
				wakeTime = expiryTime;
		}

		m_cond.wait_until(lock, wakeTime, [this]() { return ! m_run; });
	}
}

void TimerPool::stop()
{
	std::lock_guard<decltype(m_mutex)> lock(m_mutex);
	m_run = false;
	m_timers.clear();
	m_cond.notify_all();
}

// ==================

TimerPool::Timer::Timer(TimerPool& parentPool)
	: m_parent(parentPool)
	, m_running(false)
{

}

void TimerPool::Timer::setCallback(std::function<void(void)>&& callback)
{
	{
		std::lock_guard<decltype(m_mutex)> lock(m_mutex);
		m_callback = callback;
	}

	m_parent.wake();
}

void TimerPool::Timer::setInterval(std::chrono::milliseconds ms)
{
	{
		std::lock_guard<decltype(m_mutex)> lock(m_mutex);
		m_interval = ms;
	}

	m_parent.wake();
}

void TimerPool::Timer::setRepeated(bool repeated)
{
	{
		std::lock_guard<decltype(m_mutex)> lock(m_mutex);
		m_repeated = repeated;
	}

	m_parent.wake();
}

void TimerPool::Timer::start()
{
	{
		std::lock_guard<decltype(m_mutex)> lock(m_mutex);

		m_running = true;
		m_nextExpiry = TimerPool::Clock::now() + m_interval;
	}
	
	m_parent.wake();
}

void TimerPool::Timer::stop()
{
	{
		std::lock_guard<decltype(m_mutex)> lock(m_mutex);
		m_running = false;
	}

	m_parent.wake();
}

TimerPool::Clock::time_point TimerPool::Timer::fire()
{
	decltype(m_callback) callback;
	decltype(m_nextExpiry) nextExpiry;

	{
		std::lock_guard<decltype(m_mutex)> lock(m_mutex);
		if (m_repeated)
			m_nextExpiry = TimerPool::Clock::now() + m_interval;
		else
			m_nextExpiry = TimerPool::Clock::time_point::max();

		callback = m_callback;
		nextExpiry = m_nextExpiry;
	}

	if (callback)
		callback();

	return nextExpiry;
}

TimerPool::Clock::time_point TimerPool::Timer::nextExpiry() const
{
	std::lock_guard<decltype(m_mutex)> lock(m_mutex);

	if (! m_running)
		return TimerPool::Clock::time_point::max();

	return m_nextExpiry;
}
