"""Exceptions raised by resmoke.py."""


class ResmokeError(Exception):  # noqa: D204
    """Base class for all resmoke.py exceptions."""
    pass


class SuiteNotFound(ResmokeError):  # noqa: D204
    """A suite that isn't recognized was specified."""
    pass


class StopExecution(ResmokeError):  # noqa: D204
    """Exception raised when resmoke.py should stop executing tests if failing fast is enabled."""
    pass


class UserInterrupt(StopExecution):  # noqa: D204
    """Exception raised when a user signals resmoke.py to unconditionally stop executing tests."""
    pass


class TestFailure(ResmokeError):  # noqa: D204
    """Exception raised by a hook in the after_test method.

    Raised if it determines the the previous test should be marked as a failure.
    """
    pass


class ServerFailure(TestFailure):  # noqa: D204
    """Exception raised by a hook in the after_test method.

    Raised if it detects that the fixture did not exit cleanly and should be marked
    as a failure.
    """
    pass


class PortAllocationError(ResmokeError):  # noqa: D204
    """Exception that is raised by the PortAllocator.

    Raised if a port is requested outside of the range of valid ports, or if a
    fixture requests more ports than were reserved for that job.
    """
    pass
