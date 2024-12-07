#include <H4.h>

H4 h4(115200);

H4Delay someF() {
    Serial.printf("on 500, awaiting 400 ms:\n");
    co_await H4Delay(400);
    Serial.printf("400ms awaited! T=%u\n", millis());
}
H4Delay blinkLED() {
	while(1) {
		digitalWrite(LED_BUILTIN, HIGH);
		co_await H4Delay(1000); // Non-blocking delay
		digitalWrite(LED_BUILTIN, LOW);
		co_await H4Delay(1000);
	}
}
void h4setup(){ 

	blinkLED();

    // Test of what replaces h4Chunker(vs,[](std::vector<std::string>::iterator it){ Serial.printf("Processing [%s]\n", *it.data());}, 100,200);
    h4.queueFunction([]() -> H4Delay {
        std::vector<std::string> vs {"Hello", "World"};
        for (auto &v : vs) {
            Serial.printf ("Processing [%s]\n", v.data());
            co_await H4Delay(task::randomRange(100,200));
        }
    });

    // Test of what replaces h4.every(100, []{ Serial.printf("Some processing\n"); });
    h4.queueFunction([]() -> H4Delay {
        while (true) {
            Serial.printf("Some processing\n");
            co_await H4Delay(100);
        }
    });

    // Test of cancelling a scheduled coroutine externally, after it is executed, and before it's finished while.
    auto context = h4.once(500, someF);
    // Cancel After 100ms of primary execution.
    h4.once(600, [context]{ h4.cancel(context); });
    
    // Test of cancelling a coroutine by the corresponding task pointer.
    auto tocancel = h4.queueFunction([]() -> H4Delay { // Replacement to h4.nTimes(20, 5, []{ Serial.printf("i=%d\n", ME->nrq);}, []{Serial.printf("Chain Function\n"); });
        for (auto i = 0; i < 20; i++) {
            Serial.printf("i=%d\n", i);
            co_await H4Delay(5);
        }
        Serial.printf("Chain Function\n");
    });
    // After 20 milliseconds, cancel it.
    h4.once(20, [tocancel]{ h4.cancel(tocancel); });


    // Test of cancelling some coroutine manually, note that the H4Delay needs to receive nullptr in its second argument.
    h4.nTimes(20,5,[]() {
        auto somefunction = [] (int iteration)-> H4Delay {
            Serial.printf("Hola! %d\n", iteration);
            co_await H4Delay(100, nullptr);
            Serial.printf("Survived!\n");
        };
        auto d = somefunction(MY(nrq));
        // Cancel in the proper time.
        d._coro.promise().cancel(); 
    });
}
