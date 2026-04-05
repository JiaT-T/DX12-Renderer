#pragma once
#include <Windows.h>
class GameTimer
{
public :
	GameTimer();

	float TotalTime()const;			//运行的总时间
	float DeltaTime()const;			//获取mDelta变量
	bool IsStoped();				//获取isStoped变量

	void Reset();					//重置计时器
	void Start();					//开始计时器
	void Stop();					//停止计时器
	void Tick();					//计算每帧时间间隔
private :
	double mSecondsPerCount;		//计数器每跳动一次经过多少秒
	double mDeltaTime;				//每帧时间

	_int64 mBaseTime;				//基准时间
	_int64 mPauseTime;				//暂停的总时间
	_int64 mStopTime;				//停止时刻的时间
	_int64 mPrevTime;				//上一帧时间
	_int64 mCurrentTime;			//本帧时间

	bool isStoped;					//停止状态
};

