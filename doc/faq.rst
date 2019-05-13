Smartcard library FAQ
---------------------

Is the smartcard library complete?
"""""""""""""""""""""""""""""""""""

No, this library is still a work in progress.
More particularly, even though all the abstraction for
contact and contactless cards are present, only the
contact cards are fully supported because only these have
a proper underlying ISO7816-3 driver (for now the project
does not have an ISO14443 driver, which is a work in progress).
