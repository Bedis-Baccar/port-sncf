#include "Arc.h"

Arc::Arc(int id, ArcType ty, int o, int d, int w, int c, int t, int start_time, int end_time):ID(id),type(ty),from(o),to(d),cost(w * t + c),start_time(start_time),end_time(end_time)
{
}

Arc::Arc(int id, ArcType ty, int o, int d, int start_time, int end_time):ID(id),type(ty),from(o),to(d),start_time(start_time),end_time(end_time)
{
}


