static __attribute__((unused)) void  log_tr_format(const char* frm, ...)
{
	va_list args;
	va_start( args, frm );
	log_vformat( SECTION, LOGGING_LEVEL_TR, frm, args );
	va_end( args );
}
static __attribute__((unused)) void  log_err_format(const char* frm, ...)
{
	va_list args;
	va_start( args, frm );
	log_vformat( SECTION, LOGGING_LEVEL_ERR, frm, args );
	va_end( args );
}
static __attribute__((unused)) void  log_warn_format(const char* frm, ...)
{
	va_list args;
	va_start( args, frm );
	log_vformat( SECTION, LOGGING_LEVEL_WRN, frm, args );
	va_end( args );
}
