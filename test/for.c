#include "test.h"

static void test_for() {
	int j = -11, k = -10, m = -1000;
	int sj = 2, sk = 10, sm = 100;		// sentinel variables that should track the for loop's variables
	for (int j=2, k=10, m=100; j<4; j++, k++, m++, sj++, sk++, sm++) {
		
		int ssj = 1000;
		for (int j = 1000; j < 1002; j++, ssj++) {
			expect(j, ssj);
			//printf("inner j = %i, k = %i, m = %i\n", j, k, m);
		}
		expect(j, sj);
		expect(k, sk);
		expect(m, sm);
		//printf("middle j = %i, k = %i, m = %i\n", j, k, m);
		
	}
	expect(j, -11);
	expect(k, -10);
	expect(m, -1000);
	//printf("outer j = %i, k = %i, m = %i\n", j, k, m);
	
	
	// verify compiles okay with empty initial clause
	int i = 0, si = 0;
	for (; i<4; i++, si++) {
		expect(i, si);
		//printf("no initial clause; i = %i\n", i);
	}
}



void testmain() {
    print("for () embedded declaration tests");
    test_for();

}