// A disabled probe performs no clock reads on ordinary compilation paths.
struct IncrementalWorkTimer {
	double *counter;
	Clock::time_point started{};
	explicit IncrementalWorkTimer(double *counter) : counter(counter) {
		if (counter)
			started = Clock::now();
	}
	void stop() {
		if (counter) {
			*counter += elapsedMilliseconds(started);
			counter = nullptr;
		}
	}
	~IncrementalWorkTimer() { stop(); }
};
