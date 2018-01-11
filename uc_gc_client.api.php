<?php

/**
 * @file
 * Hooks provided by the uc_gcsubs module.
 */

/**
 * Alters GoCardless Redirect URL.
 *
 * Allows modules to alter the mandate details array before it is used to
 * compose the GoCardless Redirect URL.
 *
 * @return
 *   Array of mandate details that will be passed to GoCardless redirect url.
 */
function hook_mandate_details_alter($order_id, &$mandate_details) {

  $mandate_details['user']['first_name'] = 'Rob';
}

/**
 * Alters payments details provided to GoCardless.
 *
 * Allows modules to alter the paramaters array for new subscriptions or
 * one-off payments before a call is made to the GoCardless API.
 *
 * @return
 *   Array of paramaters that will be passed to GoCardless
 */
function hook_payment_details_alter(&$payment_details, $order, $type) {

  switch ($type) {
    case 'subscription':
      $payment_details['amount'] = $payment_details['amount'] * 2;
      break;
  }
}

/**
 * Alters values before validating a scheduled adjustment.
 *
 * Allows the sheduled adjustment amount, or the order's amount to be altered
 * before validating the adjustment.
 *
 * @param $amount
 *   The amount of the scheduled adjustment
 * @param $price
 *   The total amount for the order
 * @param $date
 *   The date of the scheduled adjustment
 * @param $order_id
 *   The order ID relating to the scheduled adjustment
 */
function hook_adjs_validate_alter(&$amount, &$price, $date, $order_id) {
  $amount = 0;
  $price = 0;
}
