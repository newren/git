#ifndef REPLAY_H
#define REPLAY_H

void replay_descendants(struct repository *repo,
			const struct object_id *prev_head,
			const struct object_id *new_head);

#endif
