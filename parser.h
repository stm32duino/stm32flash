#ifndef _H_PARSER
#define _H_PARSER

typedef struct parser     parser_t;
typedef enum   parser_err parser_err_t;

struct parser {
	const char *name;
	void*        (*init )();							/* initialise the parser */
	parser_err_t (*open )(void *storage, const char *filename, const char write);	/* open the file for read|write */
	parser_err_t (*close)(void *storage);						/* close and free the parser */
	unsigned int (*size )(void *storage);						/* get the total data size */
	parser_err_t (*read )(void *storage, void *data, unsigned int *len);		/* read a block of data */
	parser_err_t (*write)(void *storage, void *data, unsigned int len);		/* write a block of data */
};

enum parser_err {
	PARSER_ERR_OK,
	PARSER_ERR_SYSTEM,
	PARSER_ERR_INVALID_FILE,
	PARSER_ERR_WRONLY,
	PARSER_ERR_RDONLY
};

#endif
