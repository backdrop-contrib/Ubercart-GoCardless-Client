<?php

/**
 * @file
 * Functions that handle redirections and webhooks from GoCardless.com.
 */

/**
 * Menu callback; upon return from a GoCardless Billing Request Flow.
 *
 * @param int $order_id
 *   The Ubercart Order ID.
 * @param string $request_id
 *   The Billing Request ID supplied by GoCardless.
 */
function uc_gc_client_billing_request($order_id, $request_id) {
  if (intval($_SESSION['cart_order']) != $order_id) {
    backdrop_set_message(t('Thank you for your order! GoCardless will notify us once your payment has been processed.'));
    backdrop_goto('cart');
  }

  // Ensure the payment method is GoCardless.
  $order = uc_order_load($order_id);
  if ($order->payment_method != 'gc_client') {
    backdrop_goto('cart');
  }

  // Call the GoC API to get customer ID and mandate ID if there is one.
  $uid = $order->uid;
  $data = array(
    'endpoint' => 'billing_request',
    'action' => 'get',
    'id' => $request_id,
  );
  $result = uc_gc_client_post($data);
  if (isset($result->response)) {
    $billing_request = $result->response->body->billing_requests;
    $mandate_id = NULL;
    if (isset($billing_request->links->mandate_request_mandate)) {
      $mandate_id = $billing_request->links->mandate_request_mandate;
      $comment = t('Your new debit mandate @mandate has been created by GoCardless.', array(
        '@mandate' => $mandate_id,
      ));
      uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
      backdrop_set_message($comment);
    }
    $customer_id = NULL;
    if (isset($billing_request->links->customer)) {
      $customer_id = $billing_request->links->customer;
    }
  }
  else {
    backdrop_set_message(t('Something went wrong with your order! Please contact the site administrator.'), 'error');
    backdrop_goto('cart');
  }

  // Process the Instant Payment that has been created.
  if (isset($billing_request->payment_request)) {
    $payment = $billing_request->payment_request;
    $payment_amount = $payment->amount / 100;
    $country_code = $billing_request->resources->customer_bank_account->country_code;
    $currency_symbol = uc_gc_client_currency($country_code)['sign'];
    $payment_comment = t('GoCardless @payment_type payment ID @payment_id created.', array(
      '@payment_id' => $billing_request->payment_request->links->payment,
      '@payment_type' => $billing_request->fallback_occurred ? 'One-off' : 'Instant', 
    ));
    if (!$billing_request->fallback_occurred) { 
      $order_comment = t('You have made an Instant payment of @amount via GoCardless for your order #@order_id.', array(
        '@order_id' => $order_id,
        '@amount' => uc_currency_format($payment_amount, $currency_symbol),
      ));
    }
    else {
      $order_comment = t('A payment of @amount has been created via GoCardless with your debit mandate and will be paid from your bank account in 3-4 working days.', array(
        '@amount' => uc_currency_format($payment_amount, $currency_symbol),
      ));
    }

    // uc_payment_enter() will set the order status to "Completed" so should
    // only be used if there is no mandate creation. Otherwise the payment
    // needs adding to the database here.
    $data = serialize(array('payment_id' => $billing_request->payment_request->links->payment));
    if (!$mandate_id) {
      uc_payment_enter($order_id, 'gc_client', $order->order_total, $uid, $data, $payment_comment);
    }
    else {
      db_insert('uc_payment_receipts')
        ->fields(array(
          'order_id' => $order_id,
          'method' => 'gc_client',
          'amount' => $order->order_total,
          'uid' => $uid,
          'data' => $data,
          'comment' => $payment_comment,
          'received' => REQUEST_TIME,
        ))
        ->execute();
    }
    uc_order_comment_save($order_id, $uid, $order_comment, 'order', 'Completed', TRUE);
    backdrop_set_message($order_comment);
  }

  // Process any One-off payments if required.
  foreach ($order->products as $product) {
    if ($product->data['gc_auth_type'] == 'payment') {
      // Save GoCardless data about the order-product.
      $created = REQUEST_TIME;
      $data = array(
        'ucid'   => $order_id,
        'ucpid'  => $product->order_product_id,
        'gcid'   => $mandate_id,
        'gcrid'  => $request_id,
        'gccid'  => $customer_id,
        'uid'    => $uid,
        'type'   => 'P',
        'status' => 'pending',
        'created' => $created,
        'start_date' => $created,
        'updated' => $created,
        'sandbox' => config_get('uc_gc_client.settings', 'sandbox'),
      );
      db_insert('uc_gc_client')
        ->fields($data)
        ->execute();

      // Update the next payment creation date and tell customer. 
      if ($interval_params = isset($product->data['interval_params']) ? $product->data['interval_params'] : NULL) {
        $string = '+' . $interval_params['string'];
        $next_payment = strtotime($string, $created);

        // Provide a hook so that next_payment can be altered by another module.
        backdrop_alter('billing_request_next_payment', $next_payment, $order);

        db_update('uc_gc_client')
          ->fields(array(
            'next_payment' => $next_payment,
            'next_payment_uuid' => NULL,
          ))
          ->condition('ucpid', $product->order_product_id)
          ->execute();

        // Inform customer about next scheduled payment.
        $calculate = uc_gc_client_price_calculate($order, $product->order_product_id, NULL, NULL);
        $message = t("The next recurring payment of @amount for '@product_title' has been scheduled for @next_payment_date.", array(
          '@amount' => uc_currency_format($calculate['amount'], $currency_symbol),
          '@product_title' => $product->title,
          '@next_payment_date' => format_date($next_payment, 'uc_store'),
        ));
        backdrop_set_message($message);
        uc_order_comment_save($order_id, $uid, $message, 'order', 'Completed', TRUE);
      }
    }
  }

  // This lets us know it's a legitimate access of the complete page.
  $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
  backdrop_goto('cart/checkout/complete');
}

/**
 * Menu callback; return user to Checkout page after using the GoC Exit link.
 *
 *   The exit link is presented to customers by GoCardless if they cannot
 *   find their bank account while making an Instant Payment. They are also
 *   presented with an option to continue by creating a debit.
 */
function uc_gc_client_billing_request_exit() {
  backdrop_set_message(t('We are sorry but it looks like Instant Payments are not available for your bank. Please try again using the option to pay by debit. If you are unable to do this then contact the site administrator for further assistance.'), 'warning');
  backdrop_goto('cart/checkout');
}

/**
 * Menu callback; upon return from a GoCardless Redirect Flow.
 *
 * @param string $redirect
 *   The Redirect ID supplied by GoCardless.
 * @param string $mandate
 *   The Mandate ID supplied by GoCardless.
 * @param string $customer
 *   The Customer ID supplied by GoCardless.
 * @param int $order_id
 *   The Ubercart Order ID.
 * @param string $payment_method
 *   The Ubercart Payment method ID.
 * @param string $start_date
 *   The start date for the mandate.
 * @param int $cart_id
 *   The Ubercart Cart ID for the order.
 */
function uc_gc_client_redirect($redirect, $mandate, $customer, $order_id, $payment_method, $start_date = NULL, $cart_id = 0) {

  if (intval($_SESSION['cart_order']) != $order_id) {
    backdrop_set_message(t('Thank you for your order! GoCardless will notify us once your payment has been processed.'));
    backdrop_goto('cart');
  }

  // Ensure the payment method is GoCardless.
  $order = uc_order_load($order_id);
  if ($order->payment_method != 'gc_client') {
    backdrop_goto('cart');
  }

  // Let customer know mandate has been created.
  $uid = $order->uid;
  $comment = t('Your new debit mandate @mandate has been created by GoCardless.', array('@mandate' => $mandate));
  uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
  backdrop_set_message($comment);

  // @todo Apply some logic here to group products into single payments / subscriptions if applicable
  // Set up seperate payments / subscriptions for each product in cart.
  foreach ($order->products as $product) {
    $ucpid = $product->order_product_id;
    $product->data['gc_auth_type'] == 'subscription' ? $gc_type = 'S' : $gc_type = 'P';

    $product_uc = db_select('uc_gc_client_products', 'p')
      ->fields('p')
      ->condition('nid', $product->nid)
      ->execute()->fetch();

    // Obtain start date.
    $start_date = uc_gc_client_start_date($product_uc);

    // Insert info about the order into the database.
    $created = REQUEST_TIME;
    $data = array(
      'ucid'   => $order_id,
      'ucpid'  => $ucpid,
      'gcid'   => $mandate,
      'gcrid'  => $redirect,
      'gccid'  => $customer,
      'uid'    => $uid,
      'type'   => $gc_type,
      'status' => 'pending',
      'created' => $created,
      'start_date' => $start_date,
      'updated' => $created,
      'sandbox' => config_get('uc_gc_client.settings', 'sandbox'),
    );
    db_insert('uc_gc_client')
      ->fields($data)
      ->execute();

    isset($product->data['interval_params']) ? $interval = $product->data['interval_params'] : NULL;

    // If it is a subscription.
    if ($gc_type == 'S') {
      $calculate = uc_gc_client_price_calculate($order, $product->order_product_id, NULL, NULL);
      $currency_code = isset($calculate['currency']) ? $calculate['currency'] : config_get('uc_store.settings', 'uc_currency_code');
      $currency_sign = isset($calculate['sign']) ? $calculate['sign'] : config_get('uc_store.settings', 'uc_currency_sign');
      $payment_details = array(
        'amount' => $calculate['amount'],
        'currency' => $currency_code,
        'name' => 'Subscription plan for ' . $product->title,
        'interval' => $interval['length'],
        'interval_unit' => $interval['unit_gc'],
        'metadata' => array(
          'ucpid' => $ucpid,
        ),
      );
      // Provide hook so that payment details can be altered by another module.
      $type = 'subscription';
      backdrop_alter('payment_details', $payment_details, $order, $type);

      $params = array(
        'endpoint' => 'subscriptions',
        'action' => 'create',
        'mandate' => $mandate,
        'amount' => $payment_details['amount'],
        'currency' => $payment_details['currency'],
        'name' => $payment_details['name'],
        'interval' => $payment_details['interval'],
        'interval_unit' => $payment_details['interval_unit'],
        'metadata' => $payment_details['metadata'],
        'idempotency_key' => uc_gc_client_uuid_generate(),
      );
      if (!$product_uc->raise_payment) {
        $params['start_date'] = format_date($start_date, 'gocardless');
      }
      $result = uc_gc_client_api($params);

      if ($result->response->status_code == 201) {
        $sub = $result->response->body->subscriptions;
        $comment_arr = array(
          '@product' => $product->title,
          '@interval' => $sub->interval,
          '@interval_unit' => $sub->interval_unit,
          '@amount' => uc_currency_format($sub->amount / 100, $currency_sign),
          '@start_date' => format_date(strtotime($sub->start_date), 'uc_store'),
        );
        $comment = t('Your @interval @interval_unit subscription plan of @amount for @product has been created with GoCardless, and the first payment will be made from your bank on @start_date.', $comment_arr);
        uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
        backdrop_set_message($comment);
      }
      else {
        if (!isset($result->response) || !$result->response || $result->response->status_code == 500) {
          backdrop_set_message(t('There was a problem creating your subscription at GoCardless, so we will resubmit it later.'), 'warning');
          db_update('uc_gc_client')
            ->fields(array(
              'next_payment' => $created,
            ))
            ->condition('ucpid', $ucpid)->execute();

          $order->products[$ucpid]->data['subs_details'] = $params;
          uc_order_save($order);
        }
        else {
          backdrop_set_message(t('Something went wrong creating your subscription with GoCardless. Please contact the site administrator for assistance.'), 'error');
        }
      }
    }

    // If it is one-off Payments.
    elseif ($gc_type != 'S') {
      // If raise_payment is set for the product then raise the first payment
      // immediately.
      if ($product_uc->raise_payment) {
        $calculate = uc_gc_client_price_calculate($order, $product->order_product_id, NULL, NULL);
        $currency_code = isset($calculate['currency']) ? $calculate['currency'] : config_get('uc_store.settings', 'uc_currency_code');
        $currency_sign = isset($calculate['sign']) ? $calculate['sign'] : config_get('uc_store.settings', 'uc_currency_sign');
        $order->data['initial_payment'] = uc_currency_format($calculate['amount'], $currency_sign);
        uc_order_save($order);
        $payment_details = array(
          'amount' => $calculate['amount'],
          'currency' => $currency_code,
          'description' => 'Initial payment for ' . $product->title,
          'metadata' => array(
            'ucpid' => $ucpid,
          ),
        );

        // Provide hook so that payment details can be altered by another
        // module.
        $type = 'payment';
        backdrop_alter('payment_details', $payment_details, $order, $type);

        $uuid = uc_gc_client_uuid_generate();
        $params = array(
          'endpoint' => 'payments',
          'action' => 'create',
          'mandate' => $mandate,
          'amount' => $payment_details['amount'],
          'currency' => $payment_details['currency'],
          'description' => $payment_details['description'],
          'metadata' => $payment_details['metadata'],
          'idempotency_key' => $uuid,
        );
        $result = uc_gc_client_api($params);

        if ($result->response->status_code == 201) {
          // Update next_payment field in gc_client table.
          $payment = $result->response->body->payments;
          $next_payment = isset($interval) ? strtotime('+' . $interval['string']) : NULL;
          db_update('uc_gc_client')
            ->fields(array(
              'next_payment' => $next_payment,
              'updated' => REQUEST_TIME,
            ))
            ->condition('ucpid', $ucpid)->execute();

          // Let everyone know what is going on.
          $comment_array = array(
            '@amount' => uc_currency_format($payment->amount / 100, $currency_sign),
            '@charge_date' => date('D d M Y', strtotime($result->response->body->payments->charge_date)),
            '@product' => $product->title,
          );
          $comment = t('An initial payment of @amount for @product has been created with GoCardless, and will be made from your bank on @charge_date.', $comment_array);
          uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
          backdrop_set_message($comment);

          // Provide a hook so that other modules can respond to payment
          // creation.
          module_invoke_all('payment_created', $payment, $order_id, 'checkout');
        }
        else {
          backdrop_set_message(t('An initial payment could not be created because authentication failed. We will try and raise it again later.'), 'warning');

          // Update next_payment to now so that it will get picked up on next
          // cron run.
          $update = array(
            'next_payment' => $created,
            'updated' => REQUEST_TIME,
          ); 
          if (!isset($result->response) || !$result->response || $result->response->status_code == 500) {
            // Provide the next payment with the Uuid which will be re-
            // submitted in the cron run to ensure a duplicate payment isn't
            // created.
            $update['next_payment_uuid'] = $uuid;
          }
          db_update('uc_gc_client')
            ->fields($update)->condition('ucpid', $ucpid)->execute();
        }
      }

      // Else if a start date is set for the product then defer the first
      // payment creation.
      elseif (!is_null($start_date)) {
        // Update next_payment field in uc_gcsubs table.
        db_update('uc_gc_client')
          ->fields(array(
            'next_payment' => $start_date,
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucpid', $ucpid)
          ->execute();

        $calculate = uc_gc_client_price_calculate($order, $product->order_product_id, NULL, NULL);
        $currency_code = isset($calculate['currency']) ? $calculate['currency'] : config_get('uc_store.settings', 'uc_currency_code');
        $currency_sign = isset($calculate['sign']) ? $calculate['sign'] : config_get('uc_store.settings', 'uc_currency_sign');
        $order->data['initial_payment'] = uc_currency_format($calculate['amount'], $currency_sign);
        uc_order_save($order);

        // Let everyone know what is going on.
        $comment = t('A payment for @amount will be created with GoCardless on @start_date.', array(
          '@amount' => uc_currency_format($calculate['amount'], $currency_sign),
          '@start_date' => format_date($start_date, 'uc_store'),
        ));
        uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
        backdrop_set_message($comment);
      }
    }
  }

  // This lets us know it's a legitimate access of the complete page.
  $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
  backdrop_goto('cart/checkout/complete');
}

/**
 * Returns a start date for one-off or subscription payments.
 *
 * @param object $product_uc
 *   The Ubercart Product GoCardless configuration data.
 *
 * @return mixed
 *   The start date as a Unix timestamp, or NULL.
 */
function uc_gc_client_start_date($product_uc) {
  if ($product_uc->raise_payment) {
    $start_date = REQUEST_TIME;
  }
  else {
    // TODO should this be >= $next_possible_charge_date ?
    if ($product_uc->start_date >= REQUEST_TIME) {
      $start_date = $product_uc->start_date;
    }
    else {
      // Get the first available day of month if configurred. 
      if ($doms = config_get('uc_gc_client.settings', 'dom')) {
        if (!empty($doms)) {
          $est_charge_date = uc_gc_client_working_days_calculate('+', 4, date('Y-m-d'));
          $doms = explode(', ', $doms);
          $dates = array();
          foreach ($doms as $dom) {
            $dom >= date('d', strtotime($est_charge_date)) ? $month = 'last' : $month = 'this';
            $time = strtotime('+' . $dom . ' days', strtotime('last day of ' . $month . ' month'));
            $dates[] = $time;
          }
          sort($dates);
          $start_date = array_shift($dates);
        }
      }
      else {
        $start_date = NULL;
      }
    }
    if ($start_date) {
      $params = array(
        'endpoint' => 'mandates',
        'action' => 'get',
        'mandate' => $mandate,
      );
      $result = uc_gc_client_api($params);
      $next_possible_charge_date = $result->response->body->mandates->next_possible_charge_date;
      if ($start_date < strtotime($next_possible_charge_date)) {
        $start_date = strtotime($next_possible_charge_date);
      }
    }
  }
  return $start_date;
}

/**
 * Fetch all HTTP request headers.
 *
 * Required for Nginx servers that do not support getallheaders().
 *
 * @return mixed
 *   An associative array of all the HTTP headers in the current request, or
 *   FALSE on failure.
 */
function uc_gc_client_getallheaders() {
  $headers = array();
  foreach ($_SERVER as $name => $value) {
    if (substr($name, 0, 5) == 'HTTP_') {
      $headers[str_replace(' ', '-', ucwords(strtolower(str_replace('_', ' ', substr($name, 5)))))] = $value;
    }
  }
  return $headers;
}

/**
 * Timing attack safe string comparison.
 *
 * Compares two strings using the same time whether they're equal or not.
 * Required when hash_equals() function is not present (PHP < 5.6.0).
 *
 * @param string $known_string
 *   The string of known length to compare against.
 * @param string $user_string
 *   The user-supplied string.
 *
 * @return bool
 *   Returns TRUE when the two strings are equal, FALSE otherwise.
 */
function uc_gc_client_hash_equals($known_string, $user_string) {
  $ret = 0;
  if (strlen($known_string) !== strlen($user_string)) {
    $user_string = $known_string;
    $ret = 1;
  }
  $res = $known_string ^ $user_string;
  for ($i = strlen($res) - 1; $i >= 0; --$i) {
    $ret |= ord($res[$i]);
  }
  return !$ret;
}

/**
 * Menu callback; respond to a webhook from GoCardless.
 */
function uc_gc_client_webhook() {
  $config = config('uc_gc_client.settings');
  $secret = $config->get('sandbox') ? $config->get('webhook_sandbox') : $config->get('webhook_live');
  $webhook = file_get_contents('php://input');
  $headers = function_exists('getallheaders') ? getallheaders() : uc_gc_client_getallheaders();
  $provided_signature = $headers["Webhook-Signature"];
  $calculated_signature = hash_hmac("sha256", $webhook, $secret);

  $hash_equals_function = function_exists('hash_equals') ? 'hash_equals' : 'uc_gc_client_hash_equals';
  if ($hash_equals_function($provided_signature, $calculated_signature)) {
    $data = json_decode($webhook, TRUE);
    // Optionally write webhook to log.
    if ($config->get('debug_webhook')) {
      $message = '<pre>' . t('GoCardless webhook:') . '<br />' . print_r($data, TRUE) . '</pre>';
      watchdog("uc_gc_client", $message);
    }

    // Send a success header before processing webhooks, in order to
    // prevent timeouts.
    backdrop_add_http_header('Status', '200 OK');

    // Process the events.
    foreach ($data['events'] as $event) {
      switch ($event['resource_type']) {

        case 'mandates':
          $order_id = uc_gc_client_id($event['links']['mandate']);
          if ($order = uc_order_load($order_id)) {
            $resource = array();
            uc_gc_client_webhook_mandates($order, $event['action'], $event);
          }
          break;

        case 'subscriptions':
          $order_id = uc_gc_client_id($event['links']['mandate']);
          $order = uc_order_load($order_id);
          $resource = array();
          uc_gc_client_webhook_subscriptions($order, $event['action'], $event);
          break;

        case 'payments':
          $payment_id = $event['links']['payment'];
          $params = array(
            'endpoint' => 'payments',
            'action' => 'get',
            'id' => $payment_id,
          );
          $result = uc_gc_client_api($params);
          if (isset($result->response)) {
            $resource = $result->response->body->payments;
            $order_id = uc_gc_client_id($resource->links->mandate);
            if ($order = uc_order_load($order_id)) {
              uc_gc_client_webhook_payments($order, $event['action'], $event, $resource);
            }
            else {
              $order_id = TRUE;
              uc_gc_client_webhook_instant_payments($event['action'], $event, $resource);
            }
          }  
          break;

        default:
          $order_id = FALSE;
          break;
      }

      // Invoke Rules event.
      if (module_exists('rules') && $order_id) {
        rules_invoke_event('uc_gc_client_webhook', json_encode($event), json_encode($resource), $order_id);
      }
    }
  }
  else {
    backdrop_add_http_header('Status', '498 Invalid Token');
  }
}

/**
 * Processes 'mandates' webhooks.
 *
 * @param object $order
 *   The Ubercart Order that the webhook pertains to.
 * @param string $action
 *   The action type of GoCardless webhook event.
 * @param array $event
 *   The GoCardless webhook event.
 */
function uc_gc_client_webhook_mandates($order, $action, array $event) {
  switch ($action) {

    case 'submitted':
      if ($order->order_status <> 'processing') {
        $comment = t('Your debit mandate @mandate has been submitted to your bank by GoCardless and will be processed soon.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'processing', FALSE);
        uc_order_update_status($order->order_id, 'processing');
        // Update the status on the database for all products in this order.
        db_update('uc_gc_client')
          ->fields(array(
            'status' => 'pending',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;

    case 'failed':
      if ($order->order_status <> 'mandate_failed') {
        $comment = t('Your debit mandate @mandate creation has failed.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'processing', TRUE);

        uc_order_update_status($order->order_id, 'mandate_failed');
      }
      break;

    case 'active':
      if ($order->order_status <> 'mandate_active') {
        $comment = t('Your debit mandate @mandate has been set up successfully with your bank.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'Completed', TRUE);
        uc_order_update_status($order->order_id, 'mandate_active');
        db_update('uc_gc_client')
          ->fields(array(
            'status' => 'completed',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;

    case 'cancelled':
      if ($order->order_status <> 'canceled') {
        $comment = t('Your debit mandate @mandate has been cancelled with your bank by GoCardless.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'canceled', TRUE);

        uc_order_update_status($order->order_id, 'canceled');
        db_update('uc_gc_client')
          ->fields(array(
            'status' => 'canceled',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;

    case 'reinstated':
      if ($order->order_status <> 'processing') {
        $comment = t('Your debit mandate @mandate has been reinstated at GoCardless.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'processing', FALSE);
        uc_order_update_status($order->order_id, 'processing');
        db_update('uc_gc_client')
          ->fields(array(
            'status' => 'pending',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;
  }
}

/**
 * Processes 'payments' webhooks.
 *
 * @param object $order
 *   The Ubercart Order that the webhook pertains to.
 * @param string $action
 *   The action type of GoCardless webhook event.
 * @param array $event
 *   The GoCardless webhook event.
 * @param object $resource
 *   The GoCardless payment resource that the webhook pertains to.
 */
function uc_gc_client_webhook_payments($order, $action, array $event, $resource) {
  $order_id = $order->order_id;
  $amount = $resource->amount / 100;
  $status = db_select('uc_orders', 'o')
    ->fields('o', array('order_status'))
    ->condition('order_id', $order_id)
    ->execute()->fetchField();

  !empty($order->billing_country) ? $country_code = $order->billing_country : $country_code = $order->delivery_country;
  $currency_sign = uc_gc_client_currency($country_code)['sign'];

  switch ($action) {
    case 'confirmed':
      uc_payment_enter($order_id, 'gc_client', $amount, 0, NULL, t('Debit payment has been taken by GoCardless'));
      $comment = t('Your payment of @amount has been confirmed by GoCardless and will be paid from your bank account.', array('@amount' => uc_currency_format($amount, $currency_sign)));
      uc_order_comment_save($order_id, 0, $comment, 'order', $status, TRUE);
      // Update status to payment_received if it is the first one.
      if ($order->order_status == 'mandate_active') {
        uc_order_update_status($order->order_id, 'payment_received');
      }
      break;

    case 'cancelled':
      $comment = t("Your debit payment '@id' for @amount has been cancelled at GoCardless.", array(
        '@id' => $event['id'],
        '@amount' => uc_currency_format($amount, $currency_sign),
      ));
      uc_order_comment_save($order_id, 0, $comment, 'order', $status, TRUE);
      break;
  }
}

/**
 * Processes 'payments' webhooks for Instant Payments.
 *
 * @param string $action
 *   The action type of GoCardless webhook event.
 * @param array $event
 *   The GoCardless webhook event.
 * @param object $resource
 *   The GoCardless payment resource that the webhook pertains to.
 */
function uc_gc_client_webhook_instant_payments($action, array $event, $resource) {
  if (isset($resource->metadata->order_id)) {
    $payment_receipts = db_select('uc_payment_receipts', 'p')
      ->fields('p')
      ->condition('order_id', $resource->metadata->order_id)
      ->execute()->fetchAll();
    foreach ($payment_receipts as $receipt) {
      $data = unserialize($receipt->data);
      if (isset($data['payment_id']) && $data['payment_id'] == $resource->id) {

        switch ($action) {
          case 'failed':
            $comment = t('Payment ID @payment_id has failed with the following reason: "@reason".', array(
              '@reason' => $event['details']['description'],
              '@payment_id' => $resource->id, 
            ));            
            db_update('uc_payment_receipts')
              ->fields(array(
                'amount' => 0,
                'comment' => $receipt->comment . '<br />' . $comment,
              ))
              ->condition('receipt_id', $receipt->receipt_id)
              ->execute();

            uc_order_update_status($resource->metadata->order_id, 'gocardless_payment_failed');
            uc_order_comment_save($resource->metadata->order_id, 0, $comment, 'order', 'Completed', TRUE);
            break;
        }
      }
    }
  }
}

/**
 * Processes 'subscription' webhooks.
 *
 * @param object $order
 *   The Ubercart Order that the webhook pertains to.
 * @param string $action
 *   The action type of GoCardless webhook event.
 * @param array $event
 *   The GoCardless webhook event.
 */
function uc_gc_client_webhook_subscriptions($order, $action, array $event) {
}
