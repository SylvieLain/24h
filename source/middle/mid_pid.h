#ifndef	__MID_PID_H__
#define __MID_PID_H__


typedef struct {
	float Target;
	float Actual;
	float Actual1;
	float Out;
	
	float Kp;
	float Ki;
	float Kd;
	
	float Error0;
	float Error1;
	float ErrorInt;
	
	float ErrorIntMax;
	float ErrorIntMin;
	
	float OutMax;
	float OutMin;
} PID_t;

void PID_Update(PID_t *p);
void PID_Init(PID_t *p);

/* 本工程 PID 实例（原 app_ui 迁入；参数由 PID_Init 建立） */
extern PID_t speed_PID;
extern PID_t turn_PID;

#endif
