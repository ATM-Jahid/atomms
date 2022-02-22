typedef double real;

typedef struct {
	real x, y;
} vecR;

typedef struct {
	vecR r, vel, acc;
} Mol;

typedef struct {
	real val, sum, sum2;
} Prop;
