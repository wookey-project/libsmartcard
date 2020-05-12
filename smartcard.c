#include "libc/stdio.h"
#include "libc/nostd.h"

#include "libiso7816.h"
/* TODO: add ISO14443 layer header here when it is implemented */
#include "api/libsmartcard.h"

/* Handling the debug output */
#include "smartcard_print.h"

/*** The following two functions help APDU fragmentation across multiple
 *   units (envelopes in T=0 and blocks in T=1, blocks for ISO14443 card, ...).
 */
/* Prepare the buffer to send */
unsigned int SC_APDU_get_encapsulated_apdu_size(SC_APDU_cmd *apdu, unsigned int *out_apdu_lc_size, unsigned int *out_apdu_le_size){
	unsigned int apdu_size, apdu_lc_size, apdu_le_size;

	if(apdu == NULL){
		return 0;
	}
	/* Compute the APDU size */
	apdu_lc_size = ((apdu->lc <= SHORT_APDU_LC_MAX) ? ((apdu->lc == 0) ? 0 : 1) : 3);
	if(apdu->send_le){
		/* apdu->send_le = 1 means short APDU encoding except if the size exceeds 256.
		 * apdu->send_le = 2 means extended APDU encoding for Le.
		 */
		if((apdu->le <= SHORT_APDU_LE_MAX) && (apdu->send_le == 1)){
                        apdu_le_size = 1;
                }
                else{
                        if(apdu_lc_size != 0){
                                apdu_le_size = 2;
                        }
                        else{
                                apdu_le_size = 3;
                        }
                }
        }
        else{
                apdu_le_size = 0;
        }
        apdu_size = 4 + apdu_lc_size + apdu->lc + apdu_le_size;

	if(out_apdu_lc_size != NULL){
		*out_apdu_lc_size = apdu_lc_size;
	}
	if(out_apdu_le_size != NULL){
		*out_apdu_le_size = apdu_le_size;
	}
	return apdu_size;
}

uint8_t SC_APDU_prepare_buffer(SC_APDU_cmd *apdu, uint8_t *buffer, unsigned int i, uint8_t block_size, int *ret){
	unsigned int apdu_size, apdu_le_size;
	unsigned int to_push, offset;
	unsigned int size = 0;
	*ret = 0;

	/* Sanity checks on the lengths */
	if(apdu->le > ((uint32_t)0x1 << 16)){
		/* Absolute limits for Lc and Le (Lc is uint16, no need to check) */
		*ret = -1;
		return 0;
	}
	if((apdu->lc > APDU_MAX_BUFF_LEN) || (apdu->le > APDU_MAX_BUFF_LEN)){
		*ret = -1;
		return 0;
	}

	/* Compute the APDU size */
	apdu_size = SC_APDU_get_encapsulated_apdu_size(apdu, NULL, &apdu_le_size);

	/* Sanity checks */
	if(apdu_size < (i * block_size)){
		*ret = -1;
		return 0;
	}
	if(apdu_size > (4 + APDU_MAX_BUFF_LEN + (2 * 3))){
		*ret = -1;
		return 0;
	}

	/* Size to push */
	to_push = ((apdu_size - (i * block_size)) > block_size) ? block_size : (apdu_size - (i * block_size));
	/* Sanity check on the pushed size.
	 * Not necessary with the previous formula, but better safe than sorry.
	 */
	if(to_push > block_size){
		/* Overlow in buffer */
		*ret = -1;
		return 0;
	}

	/* Put as much data as we can in the buffer */
	offset = i * block_size; /* offset where we begin */
	size = 0;
	while(size < to_push){
		if((size >= block_size) || (size >= 256)){
			/* Sanity check: our buffer should not exceed 256 bytes long anyways ... */
			*ret = -1;
			return 0;
		}
		/* Do we have to push CLA, IN, P1, P2? */
		if(offset == 0){
			if(size >= block_size){
				/* Overflow ... this is an error */
				*ret = -1;
				return 0;
			}
			buffer[size++] = apdu->cla;
			offset++;
			continue;
		}
		if(offset == 1){
			if(size >= block_size){
				/* Overflow ... this is an error */
				*ret = -1;
				return 0;
			}
			buffer[size++] = apdu->ins;
			offset++;
			continue;
		}
		if(offset == 2){
			if(size >= block_size){
				/* Overflow ... this is an error */
				*ret = -1;
				return 0;
			}
			buffer[size++] = apdu->p1;
			offset++;
			continue;
		}
		if(offset == 3){
			if(size >= block_size){
				/* Overflow ... this is an error */
				*ret = -1;
				return 0;
			}
			buffer[size++] = apdu->p2;
			offset++;
			continue;
		}
		/* Handle Lc */
		if((offset >= 4) && (offset < (apdu_size - apdu_le_size))){
			if(apdu->lc != 0){
				if(apdu->lc <= SHORT_APDU_LC_MAX){
					if(offset == 4){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = apdu->lc;
						offset++;
						continue;
					}
					if(offset > 4){
						if((offset-5) >= APDU_MAX_BUFF_LEN){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = apdu->data[offset-5];
						offset++;
						continue;
					}
				}
				else{
					if(offset == 4){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = 0;
						offset++;
						continue;
					}
					if(offset == 5){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = (apdu->lc >> 8) & 0xff;
						offset++;
						continue;
					}
					if(offset == 6){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = apdu->lc & 0xff;
						offset++;
						continue;
					}
					if(offset > 6){
						if((offset-7) >= APDU_MAX_BUFF_LEN){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = apdu->data[offset-7];
						offset++;
						continue;
					}
				}
			}
		}
		/* Handle Le */
		if(apdu->send_le){
			if(offset >= (apdu_size - apdu_le_size)){
				if(apdu_le_size == 1){
					if(offset == (apdu_size-1)){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = apdu->le;
						offset++;
						continue;
					}
				}
				if(apdu_le_size == 2){
					if(offset == (apdu_size-2)){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = (apdu->le >> 8) & 0xff;
						offset++;
						continue;
					}
					if(offset == (apdu_size-1)){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = apdu->le & 0xff;
						offset++;
						continue;
					}
				}
				if(apdu_le_size == 3){
					if(offset == (apdu_size-3)){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = 0x00;
						offset++;
						continue;
					}
					if(offset == (apdu_size-2)){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = (apdu->le >> 8) & 0xff;
						offset++;
						continue;
					}
					if(offset == (apdu_size-1)){
						if(size >= block_size){
							/* Overflow ... this is an error */
							*ret = -1;
							return 0;
						}
						buffer[size++] = apdu->le & 0xff;
						offset++;
						continue;
					}
				}
			}
		}
	}

	return to_push;
}

/* Print an APDU on the console */
void SC_print_APDU(SC_APDU_cmd *apdu)
{
        if(apdu == NULL){
                return;
        }

        /* Sanity check on length */
        if(apdu->lc > APDU_MAX_BUFF_LEN){
                log_printf("[Smartcard] SC_print_APDU error: length %d overflow\n", apdu->lc);
                return;
        }
        log_printf("===== APDU ============\n");
        log_printf("CLA = %x, INS = %x, P1 = %x, P2 = %x\n", apdu->cla, apdu->ins, apdu->p1, apdu->p2);
        if(apdu->lc != 0){
                log_printf(", Lc = %x\n", apdu->lc);
                if(apdu->lc > 255){
                        log_printf(" (extended)");
                }
        }
        else{
                log_printf(", No Lc\n");

        }
        if(apdu->send_le != 0){
                log_printf(", Le = %x\n", apdu->le);
                if((apdu->le > 256) || (apdu->send_le == 2)){
                        log_printf(" (extended)");
                }
        }
        else{
                log_printf(", No Le\n");
        }
        log_printf("\n");
	hexdump(apdu->data, apdu->lc);

        return;
}


/* Print a RESP on the console */
void SC_print_RESP(SC_APDU_resp *resp)
{
        if(resp == NULL){
                return;
        }

        /* Sanity check on length */
        if(resp->le > APDU_MAX_BUFF_LEN){
                log_printf("[Smartcard] SC_print_RESP error: length %d overflow\n", resp->le);
                return;
        }
        log_printf("===== RESP ============\n");
        log_printf("SW1 = %x, SW2 = %x, Le = %x\n", resp->sw1, resp->sw2, resp->le);
	hexdump(resp->data, resp->le);

        return;
}

/* Print the Card information on the console */
void SC_print_Card(SC_Card *card)
{
        if(card == NULL){
		goto err;
	}
        switch(card->type){
                case SMARTCARD_CONTACT:
		        log_printf("===== Contact Card ============\n");
		        log_printf("Protocol is T = %x\n", card->T_protocol);
                        SC_iso7816_print_ATR(&(card->info.atr));
                        break;
                case SMARTCARD_NFC:
                case SMARTCARD_UNKNOWN:
                default:
                        log_printf("[Smartcard] Print Cards information: Unsupported asked smartcard type %d\n", card->type);
                        goto err;
        }

err:
	return;

}

/* Abstract Send APDU/Receive response  function */
int SC_send_APDU(SC_APDU_cmd *apdu, SC_APDU_resp *resp, SC_Card *card){
        if((apdu == NULL) || (resp == NULL) || (card == NULL)){
                goto err;
        }

        switch(card->type){
                case SMARTCARD_CONTACT:
                        return SC_iso7816_send_APDU(apdu, resp, &(card->info.atr), card->T_protocol);
                        break;
                case SMARTCARD_NFC:
                case SMARTCARD_UNKNOWN:
                default:
                        log_printf("[Smartcard] Send APDU: Unsupported asked smartcard type %d\n", card->type);
                        goto err;
        }

        return 0;
err:
        return -1;
}

static volatile bool map_voluntary;

int SC_fsm_early_init(sc_map_mode_t map_mode)
{
    switch (map_mode) {
        case SC_MAP_AUTO:
            map_voluntary = false;
            /* Early init what is necessary */
            if(SC_iso7816_fsm_early_init(SC_7816_MAP_AUTO)){
                goto err;
            }
            break;
        case SC_MAP_VOLUNTARY:
            map_voluntary = true;
            /* Early init what is necessary */
            if(SC_iso7816_fsm_early_init(SC_7816_MAP_VOLUNTARY)){
                goto err;
            }
            break;
        default:
            log_printf("invalid map mode\n");
            goto err;
    }

	return 0;
err:
	return -1;
}

int SC_fsm_map(void)
{
    if (map_voluntary) {
        return SC_iso7816_fsm_map();
    }
    return 0;

}

int SC_fsm_unmap(void)
{
    if (map_voluntary) {
        return SC_iso7816_fsm_unmap();
    }
    return 0;
}

int SC_fsm_init(SC_Card *card, uint8_t do_negiotiate_pts, uint8_t do_change_baud_rate, uint8_t do_force_protocol, uint32_t do_force_etu){
        if(card == NULL){
                return -1;
        }

	/* Try to init in all the modes we have, contact and then contactless  */
	if(SC_iso7816_fsm_init(&(card->info.atr), &(card->T_protocol), do_negiotiate_pts, do_change_baud_rate, do_force_protocol, do_force_etu) == 0){
		card->type = SMARTCARD_CONTACT;
		goto end;
	}
	else{
		goto err;
	}

end:
        return 0;
err:
        return -1;
}

void SC_smartcard_lost(SC_Card *card){
        if(card == NULL){
		goto err;
	}
        switch(card->type){
                case SMARTCARD_CONTACT:
                        SC_iso7816_smartcard_lost();
                        break;
                case SMARTCARD_NFC:
                case SMARTCARD_UNKNOWN:
                default:
                        log_printf("[Smartcard] Print Cards information: Unsupported asked smartcard type %d\n", card->type);
                        goto err;
        }

err:
	return;
}

uint8_t SC_is_smartcard_inserted(SC_Card *card){
	int ret = 0;

        if(card == NULL){
		goto err;
	}
        switch(card->type){
                case SMARTCARD_CONTACT:
                        ret = SC_iso7816_is_smartcard_inserted();
                        break;
                case SMARTCARD_NFC:
                case SMARTCARD_UNKNOWN:
                default:
                        log_printf("[Smartcard] Print Cards information: Unsupported asked smartcard type %d\n", card->type);
                        goto err;
        }


	return ret;
err:
	return 0;
}

int SC_wait_card_timeout(SC_Card *card){
	int ret = 0;

        if(card == NULL){
		goto err;
	}
        switch(card->type){
                case SMARTCARD_CONTACT:
                        ret = SC_iso7816_wait_card_timeout(&(card->info.atr), card->T_protocol);
                        break;
                case SMARTCARD_NFC:
                case SMARTCARD_UNKNOWN:
                default:
                        log_printf("[Smartcard] Print Cards information: Unsupported asked smartcard type %d\n", card->type);
                        goto err;
        }

	return ret;
err:
	return -1;
}

int SC_register_user_handler_action(SC_Card *card, void (*action)(void)){
        if(card == NULL){
		goto err;
	}
        switch(card->type){
                case SMARTCARD_CONTACT:
                        SC_iso7816_register_user_handler_action(action);
                        break;
                case SMARTCARD_NFC:
                case SMARTCARD_UNKNOWN:
                default:
                        log_printf("[Smartcard] Print Cards information: Unsupported asked smartcard type %d\n", card->type);
                        goto err;
        }

	return 0;
err:
	return -1;

}
