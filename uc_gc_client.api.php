<?php

/**
 * @file
 * Hooks provided by the uc_gc_client module.
 */

/**
 * Alters GoCardless Redirect URL.
 *
 * Allows modules to alter the mandate details array before it is used to
 * compose the GoCardless Redirect URL.
 *
 * @param int $order_id
 *   The Ubercart order ID for the GoCardless mandate that is being created.
 * @param array $mandate_details
 *   The alterable mandate details.
 */
function hook_mandate_details_alter($order_id, array &$mandate_details) {
  $mandate_details['user']['first_name'] = 'Rob';
}

/**
 * Alters payments details provided to GoCardless.
 *
 * Allows modules to alter the paramaters array for new subscriptions or
 * one-off payments before a call is made to the GoCardless API.
 *
 * @param array $payment_details
 *   The alterable payment details.
 * @param object $order
 *   The Ubercart order for the subscription or payment.
 * @param string $type
 *   The GoCardless API endpoint, either "payment", or "subscription".
 */
function hook_payment_details_alter(array &$payment_details, $order, $type) {
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
 * @param float $amount
 *   The amount of the scheduled adjustment.
 * @param float $price
 *   The total amount for the order.
 * @param string $date
 *   The date of the scheduled adjustment.
 * @param int $order_id
 *   The order ID relating to the scheduled adjustment.
 */
function hook_adjs_validate_alter(&$amount, &$price, $date, $order_id) {
  $amount = 0;
  $price = 0;
}
