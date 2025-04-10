#ifndef __SSR_H__
#define __SSR_H__

namespace SSR
{
	hw_timer_t *Timer0_Cfg = NULL;
	int cycle_count = 0;
	int count1 = 0;

	int8_t pin = -1;

	/*
	*	Расчет по Алгоритму Брезенхема. Прерывание 10мс
	*/
	void IRAM_ATTR Timer0_ISR()
	{
		static int counter_ = 0;
		static int v1_ = 0;
		static int v0_ = 0;

		v1_ = ++counter_ * count1 / cycle_count;

		if (v1_ == v0_)
			digitalWrite(pin, LOW);
		else
			digitalWrite(pin, HIGH);

		v0_ = v1_;

		if (counter_ >= cycle_count)
		{
			counter_ = 0;
			v0_ = 0;
		}
	}

	/*
	*	counter_10ms - длина цикла (100 = 1сек)
	*/
	void begin(int8_t pin_number, int counter_10ms)
	{
		pin = pin_number;
		cycle_count = counter_10ms;
		pinMode(pin, OUTPUT);
		Timer0_Cfg = timerBegin(0, 40000, true); // 2000Hz
		timerAttachInterrupt(Timer0_Cfg, &Timer0_ISR, true);
		timerAlarmWrite(Timer0_Cfg, 20, true); // 10 ms
		count1 = 0;

		timerAlarmEnable(Timer0_Cfg);
	}

	void set_level(float state)
	{
		count1 = state * cycle_count;
	}
}

#endif //__SSR_H__
