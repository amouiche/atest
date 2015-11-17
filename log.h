


enum log_level {
    LOG_WARN,
    LOG_ERR
};


#define warn(format, arg...) printf( "warn: " format "\n", ##arg )
#define err(format, arg...)  printf( "err: " format "\n", ##arg )
#define dbg(format, arg...)  printf( "dbg: " format "\n", ##arg )

#define log(level, format, arg...)  printf( "%s: " format "\n", level == LOG_ERR ? "err" : "warn", ##arg )
