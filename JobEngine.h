#pragma once
#include <cstdint>
#include <random>
#include <vector>
#include "JobExecutor.h"

namespace tooibox
{
class JobEngine
{
public:
	static JobEngine& GetInstance();

	JobEngine();
	~JobEngine();

	JobExecutor* GetRandomWorker();
	JobExecutor* GetForegroundExecutor();
private:
	// TODOJM: Fetch number of cores avaliable on CPU
	constexpr static std::size_t NR_OF_EXECUTORS = 8;
	std::vector<std::unique_ptr<JobExecutor>> m_jobExecutors;
	std::random_device m_randomDevice;
	std::mt19937 m_randomGenerator;
	std::uniform_int_distribution<unsigned int> m_randomExecutorDistribution;
};
}