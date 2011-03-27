/*
 * Just maintain a long list of all the C thingies we
 * know about, so we at least have some comprehensive test..
 */
typedef struct _Structure Structure;
struct _Structure {
  int       a;
  Blah*     b;
  int       bitfield : 2;
  int       array[5];
  void       (*function)(void                  *param1,
                         int                    ,
			 const int             *param3,
			 unsigned long          param4);
};

const unsigned int global0;
int **** global1;

typedef union _Union Union;
union _Union {
  int       a;
  int       b;
};

typedef struct {
  int       member;
} Structure2;

typedef enum _Blah Blah;
enum _Blah {
  BLAH_A,
  BLAH_B,
  BLAH_C
};

void func1           (const unsigned char         *param1,
                      const void                  *param2,
		      int                          param3,
		      int                          /*param4 -- unnamed*/);

int whatever1;
extern int whatever2;


