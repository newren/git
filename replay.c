#include "git-compat-util.h"
#include "builtin.h" // For definition of cmd_replay
#include "commit.h"
#include "hash.h"
#include "hex.h"
#include "path.h"
#include "replay.h"
#include "revision.h"
#include "strbuf.h"
#include "strvec.h"

void replay_descendants(struct repository *repo,
			const struct object_id *prev_head,
			const struct object_id *new_head)
{
	struct strvec args = STRVEC_INIT;
	struct strbuf str = STRBUF_INIT;
	FILE *fp;

	strvec_pushl(&args, "replay", "--brief-stats", "--onto", NULL);
	strvec_push(&args, oid_to_hex(new_head));
	strvec_push(&args, "--ancestry-path");
	strvec_pushf(&args, "^%s", oid_to_hex(prev_head));

	fp = xfopen(git_path_replay_edit(repo), "r");
	while (strbuf_getline_lf(&str, fp) != EOF) {
		strbuf_trim(&str);
		strvec_push(&args, str.buf);
	}
	fclose(fp);
	strbuf_release(&str);
	reset_revision_walk();
	cmd_replay(args.nr, args.v, NULL, repo);
}
