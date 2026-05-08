#include "GameTimer.h"
GameTimer::GameTimer() : mSecondsPerCount(0.0),mDeltaTime(-1.0), mBaseTime(0), 
	mPauseTime(0), mStopTime(0), mPrevTime(0), mCurrentTime(0), isStoped(false)
{
	//计算计数器每秒多少次，并存入countsPerSec中返回
	_int64 countsPerSec;
	// 获取硬件定时器的频率
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	mSecondsPerCount = 1.0 / (double)countsPerSec;
}

void GameTimer::Tick()
{
	if (isStoped)
	{
		mDeltaTime = 0.0;
		return;
	}
	//获取本帧开始时刻
	_int64 currentTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);
	mCurrentTime = currentTime;
	//计算与前一帧的时间差
	mDeltaTime = (mCurrentTime - mPrevTime) * mSecondsPerCount;
	//准备下一次计算
	mPrevTime = mCurrentTime;
	//确保时间差不为负
	if (mDeltaTime < 0.0)
	{
		mDeltaTime = 0.0;
	}
}

float GameTimer::DeltaTime()const
{
	return (float)mDeltaTime;
}

void GameTimer::Reset()
{
	_int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	
	mBaseTime = currTime;							//将当前时刻作为基准时间
	mPrevTime = currTime;				  //因为重置了，所以没有上一帧的时间
	mStopTime = 0;
	isStoped = false;
}

void GameTimer::Stop()
{
	_int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	//讲当前时刻作为停止时间
	mStopTime = currTime;
	//修改停止状态
	isStoped = true;
}

void GameTimer::Start()
{
	_int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
	//如果是停止状态，则进行以下操作
	if (isStoped)
	{
		//计算暂停总时间
		mPauseTime += (startTime - mStopTime);
		//修改停止状态
		mPrevTime = startTime;				//与Reset函数中的操作相同，重置上一帧时刻
		mStopTime = 0;
		isStoped = false;											   //关闭停止状态
	}
	//如果不是停止状态，则什么也不做
}

float GameTimer::TotalTime()const
{
	//如果当前还处于停止状态，使用停止开始的时间，减去之前的总停止时间
	//再减去开始的基准时间
	if (isStoped)
	{
		return (float)(((mStopTime - mPauseTime) - mBaseTime) * mSecondsPerCount);
	}
	//如果没有被停止，就使用当前时间，减去总停止时间
	//再减去开始的基准时间
	else
	{
		return (float)(((mCurrentTime - mPauseTime) - mBaseTime) * mSecondsPerCount);
	}
}

bool GameTimer::IsStoped()
{
	return isStoped;
}