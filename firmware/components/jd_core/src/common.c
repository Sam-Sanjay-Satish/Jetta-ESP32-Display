#include "jd/common.h"

const char *jd_err_name(jd_err_t e)
{
    switch (e) {
    case JD_OK:             return "ok";
    case JD_ERR_BUSY:       return "busy";
    case JD_ERR_ARG:        return "bad request";
    case JD_ERR_TX_REFUSED: return "tx refused";
    case JD_ERR_TIMEOUT:    return "timeout";
    case JD_ERR_NEGATIVE:   return "negative reply";
    case JD_ERR_PROTOCOL:   return "protocol error";
    case JD_ERR_CLOSED:     return "closed";
    }
    return "?";
}
