#if HAVE_CONFIG_H
#include <config.h>
#endif

#include "pixz.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

typedef enum {
  OP_WRITE,
  OP_READ,
  OP_EXTRACT,
  OP_LIST
} pixz_op_t;

static bool strsuf(const char *big, const char *small);
static char *subsuf(char *in, char *suf1, char *suf2);
static char *auto_output(pixz_op_t op, char *in);

static void usage(const char *msg) {
  if (msg)
    fprintf(stderr, "%s\n\n", msg);

  fprintf(
      stderr,
      "pixz: Parallel Indexing XZ compression, fully compatible with XZ\n"
      "\n"
      "Basic usage:\n"
      "  pixz input output.pxz           # Compress a file in parallel\n"
      "  pixz -d input.pxz output        # Decompress\n"
      "\n"
      "Tarballs:\n"
      "  pixz input.tar output.tpxz      # Compress and index a tarball\n"
      "  pixz -d input.tpxz output.tar   # Decompress\n"
      "  pixz -l input.tpxz              # List tarball contents very fast\n"
      "  pixz -x path/to/file < input.tpxz | tar x  # Extract one file very "
      "fast\n"
      "  tar -Ipixz -cf output.tpxz dir  # Make tar use pixz automatically\n"
      "\n"
      "Input and output:\n"
      "  pixz < input > output.pxz       # Same as `pixz input output.pxz`\n"
      "  pixz -i input -o output.pxz     # Ditto\n"
      "  pixz [-d] input                 # Automatically choose output "
      "filename\n"
      "\n"
      "Basic options:\n"
      "  -z, --compress            force compression\n"
      "  -d, --decompress          force decompression\n"
      "  -c, --stdout              write to standard output and don't delete input files\n"
      "  -i, --input=file          specify input file\n"
      "  -o, --output=file         specify output file\n"
      "  -t, --no-tar              don't assume input is in tar format\n"
      "  -k, --keep                keep (don't delete) input files\n"
      "  -h, --help                display this short help and exit\n"
      "  -p, --processes=NUM       use at most NUM threads; the default is 0; set to 0\n"
      "                            to use as many threads as there are processor cores\n"
      "  -T, --threads=NUM         same as -p (compatibility with xz)\n"
      "  -f, --block-fraction=NUM  TODO\n"
      "  -q, --qsize=NUM           TODO\n"
      "  -0, -1, ..., -9           set compression level, from fastest to strongest\n"
      "  -0, --fast                fastest compression level\n"
      "  -9, --best                best/strongest compression level\n"
      "  -e, --extreme             try to improve compression ratio by using more CPU time;\n"
      "                            does not affect decompressor memory requirements\n"
      "\n"
      "Tarball specific options:\n"
      "  -l, --list                list files\n"
      "  -x, --extract             extract files\n"
      "\n"
      "pixz %s\n"
      "(C) 2009-2012 Dave Vasilevsky <dave@vasilevsky.ca>\n"
      "https://github.com/vasi/pixz\n"
      "You may use this software under the FreeBSD License\n",
      PACKAGE_VERSION);
  exit(2);
}

int main(int argc, char **argv) {
  uint32_t level = LZMA_PRESET_DEFAULT;
  bool tar = true;
  bool keep_input = false;
  bool extreme = false;
  pixz_op_t op = OP_WRITE;

  char *ipath = NULL;
  char *opath = NULL;

  int ch;
  char *optend;
  long optint;
  double optdbl;

  static struct option long_options[] = {
    { "compress",       no_argument,       NULL, 'z' },
    { "decompress",     no_argument,       NULL, 'd' },
    { "stdout",         no_argument,       NULL, 'c' },
    { "extract",        no_argument,       NULL, 'x' },
    { "list",           no_argument,       NULL, 'l' },
    { "input",          required_argument, NULL, 'i' },
    { "output",         required_argument, NULL, 'o' },
    { "no-tar",         no_argument,       NULL, 't' },
    { "keep",           no_argument,       NULL, 'k' },
    { "help",           no_argument,       NULL, 'h' },
    { "processes",      required_argument, NULL, 'p' },
    { "threads",        required_argument, NULL, 'T' },
    { "block-fraction", required_argument, NULL, 'f' },
    { "qsize",          required_argument, NULL, 'q' },
    { "extreme",        no_argument,       NULL, 'e' },
    { "fast",           no_argument,       NULL, '0' },
    { "best",           no_argument,       NULL, '9' },
    { NULL,             0,                 NULL, 0   }
  };

  while ((ch = getopt_long(argc, argv, "zdcxli:o:tkhp:T:f:q:e0123456789", long_options, NULL)) != -1) {
    switch (ch) {
      case 'c':
        break;
      case 'd':
        op = OP_READ;
        break;
      case 'x':
        op = OP_EXTRACT;
        break;
      case 'l':
        op = OP_LIST;
        break;
      case 'i':
        ipath = optarg;
        break;
      case 'o':
        opath = optarg;
        break;
      case 't':
        tar = false;
        break;
      case 'k':
        keep_input = true;
        break;
      case 'h':
        usage(NULL);
        break;
      case 'e':
        extreme = true;
        break;
      case 'f':
        optdbl = strtod(optarg, &optend);
        if (*optend || optdbl <= 0)
          usage("Need a positive floating-point argument to -f");
        gBlockFraction = optdbl;
        break;
      case 'p':
      case 'T':
        optint = strtol(optarg, &optend, 10);
        if (optint < 0 || *optend)
          usage("Need a non-negative integer argument to -p");
        gPipelineProcessMax = optint;
        break;
      case 'q':
        optint = strtol(optarg, &optend, 10);
        if (optint <= 0 || *optend)
          usage("Need a positive integer argument to -q");
        gPipelineQSize = optint;
        break;
      default:
        if (ch >= '0' && ch <= '9') {
          level = ch - '0';
        } else {
          usage("");
        }
    }
  }

  argc -= optind;
  argv += optind;

  gInFile = stdin;
  gOutFile = stdout;
  bool iremove = false;
  if (op != OP_EXTRACT && argc >= 1) {
    if (argc > 2 || (op == OP_LIST && argc == 2))
      usage("Too many arguments");
    if (ipath)
      usage("Multiple input files specified");
    ipath = argv[0];

    if (argc == 2) {
      if (opath)
        usage("Multiple output files specified");
      opath = argv[1];
    } else if (op != OP_LIST) {
      iremove = true;
      opath = auto_output(op, argv[0]);
      if (!opath)
        usage("Unknown suffix");
    }
  }

  if (ipath && !(gInFile = fopen(ipath, "r")))
    die("can not open input file: %s: %s", ipath, strerror(errno));

  if (opath) {
    if (gInFile == stdin) {
      // can't read permissions of original file, because we read from stdin,
      // using umask permissions
      if (!(gOutFile = fopen(opath, "w")))
        die("can not open output file: %s: %s", opath, strerror(errno));

    } else {
      // read permissions of original file,
      // use them to create / open output file
      struct stat input_stat;
      int output_fd;

      stat(ipath, &input_stat);

      if ((output_fd = open(opath, O_CREAT | O_WRONLY, input_stat.st_mode)) ==
          -1)
        die("can not open output file: %s: %s", opath, strerror(errno));

      if (!(gOutFile = fdopen(output_fd, "w")))
        die("can not open output file: %s: %s", opath, strerror(errno));
    }
  }

  switch (op) {
    case OP_WRITE:
      if (isatty(fileno(gOutFile)) == 1)
        usage("Refusing to output to a TTY");
      if (extreme)
        level |= LZMA_PRESET_EXTREME;
      pixz_write(tar, level);
      break;
    case OP_READ:
      pixz_read(tar, 0, NULL);
      break;
    case OP_EXTRACT:
      pixz_read(tar, argc, argv);
      break;
    case OP_LIST:
      pixz_list(tar);
  }

  if (iremove && !keep_input)
    unlink(ipath);

  return 0;
}

#define SUF(_op, _s1, _s2)                                                     \
  ({                                                                           \
    if (op == OP_##_op) {                                                      \
      char *r = subsuf(in, _s1, _s2);                                          \
      if (r)                                                                   \
        return r;                                                              \
    }                                                                          \
  })

static char *auto_output(pixz_op_t op, char *in) {
  SUF(READ, ".tar.xz", ".tar");
  SUF(READ, ".tpxz", ".tar");
  SUF(READ, ".xz", "");
  SUF(WRITE, ".tar", ".tpxz");
  SUF(WRITE, "", ".xz");
  return NULL;
}

static bool strsuf(const char *big, const char *small) {
  size_t bl = strlen(big);
  size_t sl = strlen(small);

  return strcmp(big + bl - sl, small) == 0;
}

static char *subsuf(char *in, char *suf1, char *suf2) {
  if (!strsuf(in, suf1))
    return NULL;

  size_t li = strlen(in);
  size_t l1 = strlen(suf1);
  size_t l2 = strlen(suf2);

  char *r = malloc(li + l2 - l1 + 1);

  memcpy(r, in, li - l1);
  strcpy(r + li - l1, suf2);

  return r;
}
