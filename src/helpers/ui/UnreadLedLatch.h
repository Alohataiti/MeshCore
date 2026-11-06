#pragma once

// Preview retention (MSG HOLD) is independent of whether the app/user read it.
class UnreadLedLatch {
  bool pending_ = false;
  int previous_ = 0;
public:
  void arrived() { pending_ = true; }
  void read(int unread) { pending_ = false; previous_ = unread; }
  void queueChanged(int queued, int unread) { if (queued == 0) read(unread); }
  bool sample(int unread) {
    if (unread > previous_) pending_ = true;
    if (unread == 0) pending_ = false;
    previous_ = unread;
    return pending_;
  }
};
