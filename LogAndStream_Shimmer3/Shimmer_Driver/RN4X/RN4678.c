/*
 * RN4678.c
 *
 *  Created on: 30 Jun 2025
 *      Author: MarkNolan
 */

#include "RN4678.h"

#include "stdint.h"
#include <stdbool.h>

#include "RN4X.h"
#include "log_and_stream_includes.h"

volatile char btStatusStr[BT_STAT_STR_LEN_LARGEST + 1U]; /* +1 to always have a null char */
uint8_t btStatusStrIndex;
uint8_t rn4678ErrorLeds;

void RN4678_driverInit(void)
{
  RN4678_resetStatusString();
  RN4678_setErrorLedsEnabled(0);
}

void RN4678_resetStatusString(void)
{
  ShimUtil_memset_v(btStatusStr, 0, sizeof(btStatusStr));
}

void RN4678_startOfNewStatusString(void)
{
  RN4678_resetStatusString();
  btStatusStr[0U] = RN4678_STATUS_STRING_SEPARATOR;
  btStatusStrIndex = 1U;
}

/* Whether the status string holds s from index `from` */
static uint8_t RN4678_statusStrHas(uint8_t from, const char *s)
{
  while (*s)
  {
    if (btStatusStr[from++] != *s++)
    {
      return 0U;
    }
  }
  return 1U;
}

uint8_t RN4678_parseStatusString(uint8_t *waitingForArgs, uint8_t *btRxBuffPtr)
{
  uint8_t numberOfCharRemaining = 0U;
  uint8_t bringUcOutOfSleep = 0U;

  /* A status string is read in steps, each asking for the bytes still
   * missing, and a string that matches none of the patterns below can keep
   * asking - "%ERR_CONN_" followed by anything but the expected '%' asks for
   * 6 more every time. Never let it grow past btStatusStr: drop it and wait
   * for the next byte, as at the end of a completed string. */
  if ((uint16_t) btStatusStrIndex + *waitingForArgs > BT_STAT_STR_LEN_LARGEST)
  {
    memset(btRxBuffPtr, 0, *waitingForArgs);
    RN4678_resetStatusString();
    btStatusStrIndex = 0U;
    *waitingForArgs = 0U;
    setDmaWaitingForResponse(1U);
    return 0U;
  }

  ShimUtil_memcpy_v(btStatusStr + btStatusStrIndex, btRxBuffPtr, *waitingForArgs);
  memset(btRxBuffPtr, 0, *waitingForArgs);
  btStatusStrIndex += *waitingForArgs;

  /* Re-synchronise on a '%' inside the first characters. No status string has
   * a '%' among its first BT_STAT_STR_LEN_SMALLEST characters but the one it
   * opens with (the shortest that close with one, "%BONDED%" and "%REBOOT%",
   * are a character longer), so a '%' there is where the string really
   * starts. The '%' this one opened with was then the
   * closing '%' of a string the parser had lost its place in (a byte lost to
   * an overrun, for one), read in command position. Read on as before, this
   * phantom string would take the start of the real one, the real one's
   * remaining letters would be parsed as commands, and its closing '%' would
   * open the next phantom. Each connection's strings end the same way, so
   * every later status string, the connect's "%RFCOMM_OPEN%" with them, and
   * the host's commands would be lost across reconnects. Restart the string
   * at the last such '%' and read what it still lacks for the checks below. */
  if (btStatusStrIndex <= BT_STAT_STR_LEN_SMALLEST)
  {
    uint8_t i, start = 0U;

    for (i = 1U; i < btStatusStrIndex; i++)
    {
      if (btStatusStr[i] == RN4678_STATUS_STRING_SEPARATOR)
      {
        start = i;
      }
    }
    if (start)
    {
      uint8_t kept = btStatusStrIndex - start;

      for (i = 0U; i < kept; i++)
      {
        btStatusStr[i] = btStatusStr[start + i];
      }
      for (; i < btStatusStrIndex; i++)
      {
        btStatusStr[i] = '\0';
      }
      btStatusStrIndex = kept;
      *waitingForArgs = BT_STAT_STR_LEN_SMALLEST - kept;
      setDmaWaitingForResponse(*waitingForArgs);
      return 0U;
    }
  }

  enum BT_FIRMWARE_VERSION btFwVer = getBtFwVersion();

  uint8_t firstChar = btStatusStr[1U];
  switch (firstChar)
  {
    case 'A':
      /* "%AUTHENTICATED%" */
      if (btStatusStr[14U] == '%')
      {
        /* TODO */
      }
      /* "%AUTHEN" - Read outstanding bytes */
      else if (btStatusStr[6U] == 'N')
      {
        numberOfCharRemaining = BT_STAT_STR_LEN_AUTHENTICATED - BT_STAT_STR_LEN_SMALLEST;
      }
      /* "%AUTH_FAIL%" */
      else if (btStatusStr[10U] == '%')
      {
        /* TODO */
      }
      /* "%AUTH_FA" - Read outstanding bytes */
      else if (btStatusStr[6U] == 'F')
      {
        numberOfCharRemaining = BT_STAT_STR_LEN_AUTH_FAIL - BT_STAT_STR_LEN_SMALLEST;
      }
      break;
    case 'B':
      /* "%BONDED%" */
      if (btStatusStr[7U] == '%')
      {
        /* TODO */
      }
      /* "%BONDED" - Read outstanding bytes */
      else
      {
        numberOfCharRemaining = BT_STAT_STR_LEN_BONDED - BT_STAT_STR_LEN_SMALLEST;
      }
      break;
    case 'C':
      if (btStatusStr[5U] == 'E')
      {
        /* "%CONNECT,001BDC06A3D5%" - RN4678 */
        if (btStatusStr[21U] == '%')
        {
          setRn4678ConnectionState(RN4678_CONNECTED_CLASSIC);
        }
        /* "%CONNECT" for RN42 v4.77 or "%CONNECT,001BDC06A3D5," - RN42 v6.15 */
        else if ((btFwVer == RN41_V4_77 && btStatusStr[7U] == 'T')
            || (btFwVer == RN42_V4_77 && btStatusStr[7U] == 'T')
            || (btFwVer == RN42_V6_15 && btStatusStr[21U] == ','))
        {
          ShimBt_handleBtRfCommStateChange(true);
          bringUcOutOfSleep = 1U;
        }
        else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
        {
          if (btFwVer == RN41_V4_77 || btFwVer == RN42_V4_77)
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_RN42_v477_CONNECT;
          }
          else if (btFwVer == RN42_V6_15)
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_RN42_v615_CONNECT;
          }
          else
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_RN4678_CONNECT;
          }
          numberOfCharRemaining -= BT_STAT_STR_LEN_SMALLEST;
        }
      }
      else if (btStatusStr[5U] == '_')
      {
        /* "%CONN_PARAM,000C,0000,03C0%" - RN4678 */
        if (btStatusStr[26U] == '%')
        {
          //TODO
        }
        else
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_CONN_PARAM - BT_STAT_STR_LEN_SMALLEST;
        }
      }
      break;
    case 'D':
      /* "%DISCONN%" -> RN4678 */
      if (btStatusStr[8U] == '%')
      {
        /* This if is needed here for BLE connections as a
         * disconnect over BLE does not trigger an
         * RFCOMM_CLOSE status change as it does for classic
         * Bluetooth connections. */
        if (shimmerStatus.btConnected)
        {
          ShimBt_handleBtRfCommStateChange(false);
          bringUcOutOfSleep = 1U;
        }

        setRn4678ConnectionState(RN4678_DISCONNECTED);
      }
      /* "%DISCONNECT" -> RN42 */
      else if (btStatusStr[10U] == 'T')
      {
        ShimBt_handleBtRfCommStateChange(false);
        bringUcOutOfSleep = 1U;
      }
      /* "%DISCON" - Read outstanding bytes */
      else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
      {
        if (isBtDeviceRn41orRN42())
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_RN42_DISCONNECT;
        }
        else
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_RN4678_DISCONN;
        }
        numberOfCharRemaining -= BT_STAT_STR_LEN_SMALLEST;
      }
      break;
    case 'E':
      if (btStatusStr[2U] == 'N')
      {
        if (btStatusStr[5U] == 'I')
        {
          /* "%END_INQ%" */
          if (btStatusStr[8U] == '%')
          {
            /* TODO */
          }
          /* "%END_IN" - Read outstanding bytes */
          else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_END_INQ - BT_STAT_STR_LEN_SMALLEST;
          }
        }
        else if (btStatusStr[5U] == 'S')
        {
          /* "%END_SCN%" */
          if (btStatusStr[8U] == '%')
          {
            /* TODO */
          }
          /* "%END_SC" - Read outstanding bytes */
          else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_END_SCN - BT_STAT_STR_LEN_SMALLEST;
          }
        }
      }
      else if (btStatusStr[2U] == 'R')
      {
        if (btStatusStr[5U] == 'C')
        {
          /* %ERR_CON is common to two status strings. If detected, read two more chars to determine which one it is */
          /* "%ERR_CONN%" */
          if (btStatusStr[9U] == '%')
          {
            /* TODO */
          }
          /* "%ERR_CONN_PARAM%" */
          else if (btStatusStr[15U] == '%')
          {
            /* TODO */
          }
          /* "%ERR_CONN_" - Read outstanding bytes */
          else if (btStatusStr[9U] == '_')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_ERR_CONN_PARAM - BT_STAT_STR_LEN_ERR_CONN;
          }
          /* "%ERR_CON" - Read outstanding bytes */
          else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_ERR_CONN - BT_STAT_STR_LEN_SMALLEST;
          }
        }
        else if (btStatusStr[5U] == 'L')
        {
          /* "%ERR_LSEC%" */
          if (btStatusStr[9U] == '%')
          {
            /* TODO */
          }
          /* "%ERR_LSE" - Read outstanding bytes */
          else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_ERR_LSEC - BT_STAT_STR_LEN_SMALLEST;
          }
        }
        else if (btStatusStr[5U] == 'S')
        {
          /* "%ERR_SEC%" */
          if (btStatusStr[8U] == '%')
          {
            /* TODO */
          }
          /* "%ERR_SE" - Read outstanding bytes */
          else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_ERR_SEC - BT_STAT_STR_LEN_SMALLEST;
          }
        }
      }
      break;
    case 'F':
      /* "%FACTORY_RESET%" */
      if (btStatusStr[14U] == '%')
      {
        /* TODO */
      }
      /* "%FACTOR" - Read outstanding bytes */
      else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
      {
        numberOfCharRemaining = BT_STAT_STR_LEN_FACTORY_RESET - BT_STAT_STR_LEN_SMALLEST;
      }
      break;
    case 'L':
      if (btStatusStr[2U] == 'C')
      {
        /* "%LCONNECT,001BDC06A3D5,1%" - RN4678 BLE mode */
        if (btStatusStr[24U] == '%')
        {
          setRn4678ConnectionState(RN4678_CONNECTED_BLE);

          /* RN4678 seems to assume charactertic is advice once BLE connected */
          ShimBt_handleBtRfCommStateChange(true);

          bringUcOutOfSleep = 1U;
        }
        else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_RN4678_LCONNECT - BT_STAT_STR_LEN_SMALLEST;
        }
        break;
      }
      else if (btStatusStr[2U] == 'B')
      {
        /* "%LBONDED%" */
        if (btStatusStr[8U] == '%')
        {
          /* TODO */
        }
        /* "%LBONDE" - Read outstanding bytes */
        else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_LBONDED - BT_STAT_STR_LEN_SMALLEST;
        }
      }
      else if (btStatusStr[2U] == 'S')
      {
        if (btStatusStr[6U] == 'R')
        {
          /* "%LSECURED%" */
          if (btStatusStr[9U] == '%')
          {
            /* TODO */
          }
          /* "%LSECURE_FAIL%" */
          else if (btStatusStr[13U] == '%')
          {
            /* TODO */
          }
          /* "%LSECURE_F" */
          else if (btStatusStr[9U] == 'F')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_LSECURE_FAIL - BT_STAT_STR_LEN_LSECURED;
          }
          /* "%LSECUR" - Read outstanding bytes */
          else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_LSECURED - BT_STAT_STR_LEN_SMALLEST;
          }
        }
        else if (btStatusStr[6U] == 'A')
        {
          /* "%LSTREAM_OPEN%" */
          if (btStatusStr[13U] == '%')
          {
            /* TODO */
          }
          /* "%LSTREA" - Read outstanding bytes */
          else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_LSTREAM_OPEN - BT_STAT_STR_LEN_SMALLEST;
          }
        }
      }
      break;
    case 'M':
      /* "%MLDP_MODE%" */
      if (btStatusStr[10U] == '%')
      {
        /* TODO */
      }
      /* "%MLDP_M" - Read outstanding bytes */
      else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
      {
        numberOfCharRemaining = BT_STAT_STR_LEN_MLDP_MODE - BT_STAT_STR_LEN_SMALLEST;
      }
      break;
    case 'R':
      if (btStatusStr[2U] == 'E')
      {
        /* "%REBOOT%" -> RN4678 */
        if (btStatusStr[7U] == '%')
        {
          saveBtError(BT_ERROR_UNSOLICITED_REBOOT);
        }
        else
        {
          if (isBtDeviceRn41orRN42())
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_RN42_REBOOT;
          }
          else
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_RN4678_REBOOT;
          }
          numberOfCharRemaining -= BT_STAT_STR_LEN_SMALLEST;
        }
      }
      else if (btStatusStr[2U] == 'F')
      {
        if (btStatusStr[8U] == 'C')
        {
          /* "%RFCOMM_CLOSE%" */
          if (btStatusStr[13U] == '%')
          {
            ShimBt_handleBtRfCommStateChange(false);
            bringUcOutOfSleep = 1U;
          }
          /* "%RFCOMM_CLOSE" - Read outstanding bytes */
          else if (btStatusStr[13U] == '\0')
          {
            numberOfCharRemaining = BT_STAT_STR_LEN_RFCOMM_CLOSE - BT_STAT_STR_LEN_RFCOMM_OPEN;
          }
        }
        /* "%RFCOMMCLOSE%": "%RFCOMM_CLOSE%" that lost its '_' to a receive
         * overrun. That byte is the first after the "%RFCOMM" read, the one an
         * overrun takes: the module keeps sending until the receive is
         * re-armed. The string is then as long as "%RFCOMM_OPEN%" and ends in
         * '%' where that does, so it is tested for before it. */
        else if (RN4678_statusStrHas(7U, "CLOSE%"))
        {
          ShimBt_handleBtRfCommStateChange(false);
          bringUcOutOfSleep = 1U;
        }
        /* "%RFCOMM_OPEN%". The whole tail is matched, not just the closing
         * '%': a close that lost more than one byte can put a '%' there too,
         * and taking it for an open would leave the device connected to a
         * host that has gone. */
        else if (RN4678_statusStrHas(7U, "_OPEN%"))
        {
          ShimBt_handleBtRfCommStateChange(true);
          bringUcOutOfSleep = 1U;
        }
        /* "%RFCOMM" - Read outstanding bytes */
        else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_RFCOMM_OPEN - BT_STAT_STR_LEN_SMALLEST;
        }
      }
      break;
    case 'S':
      if (btStatusStr[3U] == 'C')
      {
        /* "%SECURED%" */
        if (btStatusStr[8U] == '%')
        {
          /* TODO */
        }
        /* "%SECURE_FAIL%" */
        else if (btStatusStr[12U] == '%')
        {
          /* TODO */
        }
        /* "%SECURE_F" - Read outstanding bytes */
        else if (btStatusStr[7U] == '_')
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_SECURE_FAIL - BT_STAT_STR_LEN_SECURED;
        }
        /* "%SECURE" - Read outstanding bytes */
        else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_SECURED - BT_STAT_STR_LEN_SMALLEST;
        }
      }
      else if (btStatusStr[3U] == 'S')
      {
        /* "%SESSION_OPEN%" */
        if (btStatusStr[13U] == '%')
        {
          /* TODO */
        }
        /* "%SESSION_CLOSE%" */
        else if (btStatusStr[14U] == '%')
        {
          /* TODO */
        }
        /* "%SESSION_CLOSE" - Read outstanding bytes */
        else if (btStatusStr[13U] == 'E')
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_SESSION_CLOSE - BT_STAT_STR_LEN_SESSION_OPEN;
        }
        /* "%SESSIO" - Read outstanding bytes */
        else if (btStatusStr[BT_STAT_STR_LEN_SMALLEST] == '\0')
        {
          numberOfCharRemaining = BT_STAT_STR_LEN_SESSION_OPEN - BT_STAT_STR_LEN_SMALLEST;
        }
      }
      break;
    default:
      break;
  }

  if (numberOfCharRemaining)
  {
    *waitingForArgs = numberOfCharRemaining;
  }
  else
  {
    *waitingForArgs = 0;
    numberOfCharRemaining = 1U;
  }
  setDmaWaitingForResponse(numberOfCharRemaining);
  return bringUcOutOfSleep;
}

/* The status string being read, for log-and-stream-common's
 * ShimBt_rxDropStaleStatusString(): its length so far, and dropping it so the
 * next '%' starts a new one. */
uint8_t RN4678_getStatusStrIndex(void)
{
  return btStatusStrIndex;
}

void RN4678_dropStatusString(void)
{
  RN4678_resetStatusString();
  btStatusStrIndex = 0U;
}

uint8_t RN4678_isErrorLedsEnabled(void)
{
  return rn4678ErrorLeds;
}

void RN4678_setErrorLedsEnabled(uint8_t state)
{
  rn4678ErrorLeds = state;
}
