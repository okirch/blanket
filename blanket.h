/*
 * My dear Watson, this is obviously an include file.
 */

#ifndef BLANKET_H
#define BLANKET_H

#include <sys/types.h>
#include <stdint.h>

/*
 * On-disk structure of the control file
 *
 * Bump the file version whenever making an incompatible format change
 */
#define SC_CONTROL_PATH_DEFAULT		"/etc/blanket.conf"
#define SC_CONTROL_FILE_VERSION		0x0002
#define SC_CONTROL_MAX_ENTRIES		128
#define SC_DEFAULT_GRANULARITY		8
#define SC_OUTPUT_HEADER_SIZE		1024

typedef struct sc_control_entry {
	uint16_t	dev;
	uint16_t	reserved[3];
	uint64_t	ino;
} sc_control_entry_t;

typedef struct {
	uint32_t		format;

	/* This defines the granularity for mapping the instruction counter
	 * to a pointer. Defaults to 128 for now */
	uint32_t		granularity;

	/* log2(granularity) */
	unsigned int		addr_shift;

	/* This id is copied to the output files we generate.
	 * It can be used by testing software to correlate output files
	 * to testing steps */
	uint32_t		test_id;

	int			measure_all;

	uint32_t		num_entries;
	sc_control_entry_t	entry[SC_CONTROL_MAX_ENTRIES];
} sc_control_t;

/*
 * On-disk structure of the output file
 */
typedef struct {
	uint32_t		format;
	uint32_t		dev;
	uint64_t		ino;
	struct timeval		timestamp;

	uint32_t		addr_shift;
	uint32_t		test_id;
	uint32_t		reserved[2];

	/* The first 8 bytes of the mapped 'thing'. For ELF binaries,
	 * this should contain the ELF signature */
	unsigned char		magic[8];

	char			path[512];
} sc_output_header_t;

/* In-memory context */
typedef struct {
	char *			path;
	uint16_t		flags;

	uint16_t		dev;
	uint64_t		ino;

	caddr_t			start_addr;
	caddr_t			end_addr;

	void *			map_base;
	unsigned long		map_len;

	/* log2(granularity) */
	unsigned int		addr_shift;

	/* copy of control.test_id */
	uint32_t		test_id;

	/* The first 8 bytes of the mapped 'thing'. For ELF binaries,
	 * this should contain the ELF signature */
	unsigned char		magic[8];

	unsigned int		num_counters;
	uint32_t *		counters;
} sc_object_entry_t;

typedef struct {
	const sc_control_t *	control;

	/* copy of control.granularity */
	uint32_t		granularity;

	/* copy of control.addr_shift */
	uint32_t		addr_shift;

	/* copy of control.test_id */
	uint32_t		test_id;

	unsigned int		num_entries;
	sc_object_entry_t **	entries;
} sc_context_t;

typedef struct {
	const char *		name;
	unsigned long		start_offset;
	unsigned long		end_offset;

	unsigned int		num_hits;
	double			coverage;
} sc_symbol_t;

typedef struct {
	double			global_coverage;

	unsigned long		text_offset;
	unsigned int		text_size;

	sc_symbol_t		unknown_symbol;

	unsigned int		nsymbols;
	sc_symbol_t *		symbol;
} sc_coverage_t;

struct sc_elf_object;

/* Either use the default path, or have user override via env var. */
extern const char *		sc_control_path;

extern int			sc_tracing;
extern sc_context_t *		sc_context;

extern void			sc_control_set_path(const char *path);
extern sc_control_t *		sc_control_create(void);
extern sc_control_t *		sc_control_read(void);
extern sc_control_t *		sc_control_read_quiet(void);
extern int			sc_control_write(sc_control_t *ctl);
extern int			sc_control_add_file(sc_control_t *, const char *path);
extern int			sc_control_add_dev_ino(sc_control_t *, dev_t dev, ino_t ino);
extern const sc_control_entry_t *sc_control_get_entry(const sc_control_t *, dev_t dev, ino_t ino, const char *path);

extern sc_context_t *		sc_context_init(const sc_control_t *);
extern int			sc_context_rescan(void);
extern void			sc_context_update_mapping(sc_context_t *ctx, const sc_object_entry_t *entry);
extern void			sc_context_add_sample(sc_context_t *ctx, caddr_t ip);

extern sc_object_entry_t *	sc_object_entry_clone(const sc_object_entry_t *entry);
extern void			sc_object_entry_free(sc_object_entry_t *entry);
extern void *			sc_object_entry_map_write(sc_object_entry_t *entry);
extern void			sc_object_entry_flush(sc_object_entry_t *entry);
extern sc_object_entry_t *	sc_object_entry_load(const char *path);

extern void			sc_sampling_activate_thread(void);
extern int			sc_sampling_enable(void);

/* Functions related to handling ELF objects */
extern void			sc_elf_extract_symbols(const sc_object_entry_t *, sc_coverage_t *coverage);
extern sc_coverage_t *		sc_coverage_extract(const sc_object_entry_t *);
extern void			sc_coverage_add_symbol(sc_coverage_t *, const char *, unsigned long, unsigned long);
extern void			sc_coverage_free(sc_coverage_t *);

/* Functions related to reporting */
extern int			sc_report_show(const char *path);

/*
 * procsfs access
 */
typedef struct sc_procfs_fd sc_procfs_fd_t;

extern sc_procfs_fd_t *		sc_procfs_maps_open(void);
extern const sc_object_entry_t *sc_procfs_maps_getent(sc_procfs_fd_t *);
extern void			sc_procfs_fclose(sc_procfs_fd_t *);

#endif /* BLANKET_H */
