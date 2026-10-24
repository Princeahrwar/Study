# Workspace stubs: compile cleanly out of the box (every method has a
# TODO-marked body, usually throwing "not implemented"), so the project
# builds green from minute one. Filling in the TODOs -- replacing the
# throw with real logic -- is the actual exercise. See 10_lld/00_plan.md, section 8.
add_library(lld_workspace OBJECT
  workspace/parking_lot.cpp
  workspace/elevator_system.cpp
  workspace/splitwise.cpp
  workspace/vending_machine.cpp
  workspace/movie_ticket_booking.cpp
  workspace/message_queue_pubsub.cpp
  workspace/rate_limiter.cpp
  workspace/task_scheduler.cpp
)
target_include_directories(lld_workspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
