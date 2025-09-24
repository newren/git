#ifndef REPLAY_H
#define REPLAY_H

void replay_descendants(struct repository *repo,
			struct commit *current_head,
			struct object_id *oid);

#endif
